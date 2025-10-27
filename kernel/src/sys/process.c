#include <cpu/smp.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/cmdline.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define KERNEL_STACK_SIZE   0x4000
#define USER_STACK_SIZE     0x20000

struct process* kernel_process;
static struct process* init_process;

static struct slab_cache* process_cache;
static struct slab_cache* thread_cache;
static pid_t next_pid;

static const uint16_t default_fcw = 0x33f;
static const uint32_t default_mxcsr = 0x1f80;

struct process* process_create(struct process* parent, struct pagemap* pagemap) {
    struct process* new_process = slab_cache_alloc(process_cache);
    if (unlikely(new_process == NULL)) {
        return NULL;
    }

    new_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(new_process->threads == NULL)) {
        goto error;
    }

    if (parent != NULL) {
        new_process->cwd = parent->cwd;
        VFS_NODE_REF(new_process->cwd);

        file_fork(parent, new_process);

        new_process->pagemap = pagemap_fork(parent->pagemap);
        if (unlikely(new_process->pagemap == NULL)) {
            goto error;
        }
        new_process->thread_stack_top = parent->thread_stack_top;
        new_process->brk = parent->brk;
        new_process->brk_next_unallocated_page_begin = parent->brk_next_unallocated_page_begin;

        new_process->parent = parent;
        SLIST_PUSH_FRONT(parent->children, new_process);
    } else {
        new_process->cwd = vfs_root;
        VFS_NODE_REF(vfs_root);

        new_process->pagemap = pagemap;
        new_process->thread_stack_top = PROCESS_STACK_TOP;
        new_process->brk = new_process->brk_next_unallocated_page_begin = PROCESS_BRK_BASE;
    }

    new_process->pid = __atomic_load_n(&next_pid, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    new_process->state = PROCESS_RUNNING;

    goto end;

error:
    if (new_process->threads != NULL) {
        vector_destroy(new_process->threads);
    }

    slab_cache_free(process_cache, new_process);
    new_process = NULL;
end:
    return new_process;
}

void process_create_init(void) {
    char* init_path = cmdline_get("init");
    if (!init_path) {
        init_path = "/bin/init";
    }

    struct vfs_node* init_node;
    if (vfs_lookup(vfs_root, init_path, false, NULL, &init_node) < 0) {
        kpanic(NULL, false, "failed to find %s", init_path);
    }
    init_node->ops->unlock(init_node);

    struct pagemap* init_pagemap = pagemap_create();
    if (unlikely(init_pagemap == NULL)) {
        kpanic(NULL, false, "failed to create pagemap for init process");
    }

    init_process = process_create(NULL, init_pagemap);
    if (unlikely(init_process == NULL)) {
        kpanic(NULL, false, "failed to create init process");
    }

    struct vfs_node* tty_node;
    if (vfs_lookup(vfs_root, "/dev/tty", false, NULL, &tty_node) < 0) {
        kpanic(NULL, false, "failed to find tty device");
    }
    tty_node->ops->unlock(tty_node);

    struct file* stdin_file = file_create(tty_node, O_RDONLY);
    if (unlikely(stdin_file == NULL)) {
        kpanic(NULL, false, "failed to create stdin file descriptor for init process");
    }
    init_process->fds[0].file = stdin_file;
    struct file* stdout_file = file_create(tty_node, O_WRONLY);
    if (unlikely(stdout_file == NULL)) {
        kpanic(NULL, false, "failed to create stdout file descriptor for init process");
    }
    init_process->fds[1].file = stdout_file;
    struct file* stderr_file = file_create(tty_node, O_WRONLY);
    if (unlikely(stderr_file == NULL)) {
        kpanic(NULL, false, "failed to create stderr file descriptor for init process");
    }
    init_process->fds[2].file = stderr_file;

    char* argv[] = { init_path, NULL };
    char* envp[] = { NULL };

    uintptr_t entry;

    if (elf_load(init_pagemap, init_node, &entry) < 0) {
        kpanic(NULL, false, "failed to load ELF for init process");
    }

    VFS_NODE_UNREF(init_node);

    struct thread* init_thread = thread_create_user(init_process, entry, argv, envp);
    if (unlikely(init_thread == NULL)) {
        kpanic(NULL, false, "failed to create thread for init process");
    }

    scheduler_enqueue(init_thread);
}

void process_destroy(struct process* process) {
    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        struct file* file = process->fds[i].file;
        if (file != NULL) {
            file_release(file);
        }
    }

    if (likely(process->parent != NULL)) {
        SLIST_REMOVE(process->parent->children, process);
    }

    /* reparent dying process' children to init */
    struct process* child = process->children;
    while (child != NULL) {
        struct process* next = child->next;

        child->parent = init_process;
        child->next = init_process->children;
        init_process->children = child;

        child = next;
    }

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        struct thread* thread = *vector_get(process->threads, i);
        scheduler_dequeue(thread);
        thread_destroy(thread);
    }
    vector_destroy(process->threads);

    pagemap_destroy(process->pagemap);

    slab_cache_free(process_cache, process);
}

void process_exit(struct process* process, int status) {
    if (unlikely(process->pid < 2)) {
        kpanic(NULL, false, "attempted to exit init process");
    }

    process->state = PROCESS_ZOMBIE;
    process->exit_status = status;

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        scheduler_dequeue((struct thread*) *vector_get(process->threads, i));
    }
}

void* process_sbrk(struct process* process, intptr_t size) {
    uintptr_t old_brk = process->brk;

    if (size > 0) {
        size_t remaining = process->brk_next_unallocated_page_begin - process->brk;

        if ((unsigned) size > remaining) {
            size_t bytes_needed = size - remaining;
            size_t page_count = ((bytes_needed - 1) / PAGE_SIZE_4KB) + 1;

            uintptr_t paddr = pmm_alloc(page_count);

            for (size_t i = 0; i < page_count; i++) {
                pagemap_map(process->pagemap, process->brk_next_unallocated_page_begin + (i * PAGE_SIZE_4KB),
                        paddr + (i * PAGE_SIZE_4KB), PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX, PAGE_SIZE_4KB);
            }

            process->brk_next_unallocated_page_begin += page_count * PAGE_SIZE_4KB;
        }
    } else if (size < 0) {
        uintptr_t current_page_start = process->brk_next_unallocated_page_begin - PAGE_SIZE_4KB;
        size_t remaining = process->brk - current_page_start;

        if ((unsigned) -size > remaining) {
            size_t page_count = (((-size - remaining) - 1) / PAGE_SIZE_4KB) + 1;
            for (size_t i = 0; i < page_count; i++) {
                if (process->brk_next_unallocated_page_begin - PAGE_SIZE_4KB >= PROCESS_BRK_BASE) {
                    process->brk_next_unallocated_page_begin -= PAGE_SIZE_4KB;
                    pagemap_unmap(process->pagemap, process->brk_next_unallocated_page_begin);
                }
            }
        }
    }

    process->brk += size;
    return (void*) old_brk;
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    thread->state = THREAD_READY;
    thread->is_user = false;
    thread->process = kernel_process;

    thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    thread->kernel_stack = thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    thread->registers.rdi = (uint64_t) arg;
    thread->registers.rip = entry;
    thread->registers.cs = 0x08;
    thread->registers.rflags = 0x202;
    thread->registers.ss = 0x10;
    thread->registers.rsp = thread->kernel_stack;

    spinlock_acquire(&kernel_process->lock);

    thread->tid = vector_size(kernel_process->threads);
    vector_push(kernel_process->threads, &thread);

    spinlock_release(&kernel_process->lock);
    return thread;
}

struct thread* thread_create_user(struct process* process, uintptr_t entry, char** argv, char** envp) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    thread->state = THREAD_READY;
    thread->is_user = true;
    thread->process = process;

    thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    thread->kernel_stack = thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    spinlock_acquire(&process->lock);

    thread->user_stack_paddr = pmm_alloc(USER_STACK_SIZE / PAGE_SIZE_4KB);

    uintptr_t user_stack_paddr = thread->user_stack_paddr;
    uintptr_t user_stack_vaddr = process->thread_stack_top - USER_STACK_SIZE;
    for (size_t i = 0; i < USER_STACK_SIZE / PAGE_SIZE_4KB; i++) {
        pagemap_map(process->pagemap, user_stack_vaddr + (i * PAGE_SIZE_4KB), user_stack_paddr + (i * PAGE_SIZE_4KB),
                PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX, PAGE_SIZE_4KB);
    }

    thread->registers.rip = entry;
    thread->registers.cs = 0x23;
    thread->registers.rflags = 0x202;
    thread->registers.ss = 0x1b;
    thread->registers.rsp = process->thread_stack_top;

    thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    this_cpu()->fpu_restore(thread->fpu_context);
    asm volatile("fldcw %0" :: "m"(default_fcw) : "memory");
    asm volatile("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");
    this_cpu()->fpu_save(thread->fpu_context);

    thread->fs_base = 0;
    thread->gs_base = 0;

    if (vector_size(process->threads) == 0 && argv != NULL && envp != NULL) {
        void* stack_top = (void*) (user_stack_paddr + USER_STACK_SIZE + HIGH_VMA);
        uintptr_t* stack = stack_top;

        int envp_len;
        for (envp_len = 0; envp[envp_len] != NULL; envp_len++) {
            size_t length = strlen(envp[envp_len]);
            stack = (void*) ((uintptr_t) stack - length - 1);
            memcpy(stack, envp[envp_len], length);
            *((char*) stack + length) = '\0';
        }

        int argv_len;
        for (argv_len = 0; argv[argv_len] != NULL; argv_len++) {
            size_t length = strlen(argv[argv_len]);
            stack = (void*) ((uintptr_t) stack - length - 1);
            memcpy(stack, argv[argv_len], length);
            *((char*) stack + length) = '\0';
        }

        stack = (uintptr_t*) ALIGN_DOWN((uintptr_t) stack, 16);
        if (((argv_len + envp_len + 1) & 1) != 0) {
            stack--;
        }

        uintptr_t old_rsp = thread->registers.rsp;

        *(--stack) = 0;
        stack -= envp_len;
        int i;
        for (i = 0; i < envp_len; i++) {
            old_rsp -= strlen(envp[i]) + 1;
            stack[i] = old_rsp;
        }

        *(--stack) = 0;
        stack -= argv_len;
        for (i = 0; i < argv_len; i++) {
            old_rsp -= strlen(argv[i]) + 1;
            stack[i] = old_rsp;
        }

        *(--stack) = argv_len;

        thread->registers.rsp -= (uintptr_t) stack_top - (uintptr_t) stack;
    }

    thread->tid = vector_size(process->threads);
    vector_push(process->threads, &thread);

    spinlock_release(&process->lock);
    return thread;
}

void thread_destroy(struct thread* thread) {
    spinlock_acquire(&thread->process->lock);

    vector_remove_by_value(thread->process->threads, thread);
    if (vector_size(thread->process->threads) < 1) {
        process_destroy(thread->process);
    }

    spinlock_release(&thread->process->lock);

    if (thread->is_user) {
        pmm_free((uintptr_t) thread->fpu_context - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB));
        pmm_free(thread->user_stack_paddr, USER_STACK_SIZE / PAGE_SIZE_4KB);
    }

    pmm_free(thread->kernel_stack_paddr, KERNEL_STACK_SIZE / PAGE_SIZE_4KB);

    slab_cache_free(thread_cache, thread);
}

struct thread* thread_fork(struct process* process, struct thread* old_thread) {
    struct thread* new_thread = slab_cache_alloc(thread_cache);
    if (unlikely(new_thread == NULL)) {
        return NULL;
    }

    new_thread->state = THREAD_READY;
    new_thread->is_user = true;
    new_thread->process = process;

    new_thread->kernel_stack = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB) + HIGH_VMA + KERNEL_STACK_SIZE;

    memcpy64((void*) &new_thread->registers, (const void*) &old_thread->registers, sizeof(struct registers) >> 3);
    new_thread->registers.rax = 0;

    new_thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    memcpy64(new_thread->fpu_context, old_thread->fpu_context, this_cpu()->fpu_context_size >> 3);

    new_thread->fs_base = old_thread->fs_base;
    new_thread->gs_base = old_thread->gs_base;

    spinlock_acquire(&process->lock);

    new_thread->tid = vector_size(process->threads);
    vector_push(process->threads, &new_thread);

    spinlock_release(&process->lock);
    return new_thread;
}

void process_init(void) {
    process_cache = slab_cache_create("struct process cache", sizeof(struct process));
    if (unlikely(process_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for process structs");
    }

    thread_cache = slab_cache_create("struct thread cache", sizeof(struct thread));
    if (unlikely(process_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for thread structs");
    }

    kernel_process = process_create(NULL, kernel_pagemap);
    if (unlikely(kernel_process == NULL)) {
        kpanic(NULL, false, "failed to create kernel process");
    }

    klog("[process] initialized kernel process\n");
}
