#include <cpu/asm.h>
#include <cpu/smp.h>
#include <fs/devfs.h> 
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/cmdline.h>
#include <utils/macros.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/string.h>

#define DEFAULT_FCW     0x33f
#define DEFAULT_MXCSR   0x1f80

struct process* kernel_process;
static struct process* init_process;

static struct slab_cache* process_cache;
static struct slab_cache* thread_cache;
static pid_t next_pid;

struct process* process_create(struct process* parent) {
    struct process* new_process = slab_cache_alloc(process_cache);
    if (unlikely(new_process == NULL)) {
        return NULL;
    }

    new_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(new_process->threads == NULL)) {
        goto error;
    }

    mutex_init(&new_process->fd_mutex);

    if (parent != NULL) {
        spinlock_acquire(&parent->lock);

        new_process->cwd = parent->cwd;
        VFS_NODE_REF(new_process->cwd);
        new_process->root = parent->root;
        VFS_NODE_REF(new_process->root);

        file_fork(parent, new_process);

        new_process->vmm_context = vmm_context_fork(parent->vmm_context);
        if (unlikely(new_process->vmm_context == NULL)) {
            goto error;
        }
        new_process->thread_stack_top = parent->thread_stack_top;

        new_process->parent = parent;
        SLIST_PUSH_FRONT(parent->children, new_process);

        spinlock_release(&parent->lock);
    } else {
        new_process->cwd = vfs_root;
        VFS_NODE_REF(vfs_root);
        new_process->root = vfs_root;
        VFS_NODE_REF(vfs_root);

        new_process->vmm_context = vmm_context_create();
        if (unlikely(new_process->vmm_context == NULL)) {
            goto error;
        }
        new_process->thread_stack_top = PROCESS_STACK_TOP;
    }

    new_process->pid = __atomic_load_n(&next_pid, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    new_process->state = PROCESS_RUNNING;
    return new_process;

error:
    if (new_process->threads != NULL) {
        vector_destroy(new_process->threads);
    }

    slab_cache_free(process_cache, new_process);
    return NULL;
}

void process_create_init(void) {
    char* init_path = cmdline_get("init");
    if (!init_path) {
        init_path = "/usr/bin/init";
    }

    klog("[process] starting init process %s\n", init_path);

    struct vfs_node* init_node;
    if (vfs_lookup(vfs_root, init_path, false, NULL, &init_node) < 0) {
        kpanic(NULL, false, "failed to find %s", init_path);
    }
    init_node->ops->unlock(init_node);

    init_process = process_create(NULL);
    if (unlikely(init_process == NULL)) {
        kpanic(NULL, false, "failed to create init process");
    }

    struct vfs_node* console_node;
    if (devfs_get("console", &console_node) < 0) {
        kpanic(NULL, false, "failed to find console device");
    }
    console_node->ops->unlock(console_node);

    struct file* stdin_file = file_create(console_node, O_RDONLY);
    if (unlikely(stdin_file == NULL)) {
        kpanic(NULL, false, "failed to create stdin file descriptor for init process");
    }
    init_process->fds[0].file = stdin_file;
    struct file* stdout_file = file_create(console_node, O_WRONLY);
    if (unlikely(stdout_file == NULL)) {
        kpanic(NULL, false, "failed to create stdout file descriptor for init process");
    }
    init_process->fds[1].file = stdout_file;
    struct file* stderr_file = file_create(console_node, O_WRONLY);
    if (unlikely(stderr_file == NULL)) {
        kpanic(NULL, false, "failed to create stderr file descriptor for init process");
    }
    init_process->fds[2].file = stderr_file;

    char* argv[] = { init_path, NULL };
    char* envp[] = { NULL };

    char* ld_path = NULL;

    struct auxvals auxvals;
    if (elf_load(init_process->vmm_context, 0, init_node, &auxvals, &ld_path) < 0) {
        kpanic(NULL, false, "failed to load ELF for init process");
    }

    uintptr_t entry = auxvals.at_entry.value;

    if (ld_path != NULL) {
        struct vfs_node* ld_node;
        if (vfs_lookup(vfs_root, ld_path, false, NULL, &ld_node) < 0) {
            kpanic(NULL, false, "failed to find interpreter %s", ld_node);
        }
        ld_node->ops->unlock(ld_node);

        struct auxvals ld_auxvals;
        if (elf_load(init_process->vmm_context, INTERPRETER_LOAD_BASE, ld_node, &ld_auxvals, NULL) < 0) {
            kpanic(NULL, false, "failed to load ELF for init process interpreter");
        }

        entry = ld_auxvals.at_entry.value;

        VFS_NODE_UNREF(ld_node);
        kfree(ld_path);
    }

    VFS_NODE_UNREF(init_node);

    uintptr_t user_stack_paddr = pmm_alloc(USER_STACK_SIZE / PAGE_SIZE_4KB);
    uintptr_t user_stack_vaddr = (uintptr_t) vmm_map(init_process->vmm_context, PROCESS_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, user_stack_paddr);

    uintptr_t stack_top = elf_setup_stack(user_stack_vaddr + USER_STACK_SIZE, user_stack_paddr + USER_STACK_SIZE,
            init_path, argv, envp, &auxvals);

    struct thread* init_thread = thread_create_user(init_process, entry, stack_top);
    if (unlikely(init_thread == NULL)) {
        kpanic(NULL, false, "failed to create thread for init process");
    }

    scheduler_enqueue(init_thread);
}

void process_destroy(struct process* process) {
    spinlock_acquire(&process->lock);

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        thread_destroy((struct thread*) *vector_get(process->threads, i));
    }
    vector_destroy(process->threads);

    slab_cache_free(process_cache, process);
}

// TODO: fix some of the potential race/double free issues that could occur when processes actually have multiple threads
void process_exit(struct process* process, int status) {
    if (unlikely(process->pid == 1)) {
        kpanic(NULL, false, "attempted to exit init process with status = %d", status);
    }

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        file_close(process, i);
    }

    spinlock_acquire(&process->lock);

    VFS_NODE_UNREF(process->cwd);
    VFS_NODE_UNREF(process->root);

    spinlock_acquire(&process->parent->lock);
    SLIST_REMOVE(process->parent->children, process);
    spinlock_release(&process->parent->lock);

    /* reparent dying process' children to init */
    spinlock_acquire(&init_process->lock);

    struct process* child = process->children;
    while (child != NULL) {
        struct process* next = child->next;

        child->parent = init_process;
        child->next = init_process->children;
        init_process->children = child;

        child = next;
    }

    spinlock_release(&init_process->lock);

    vmm_context_destroy(process->vmm_context);

    process->state = PROCESS_ZOMBIE;
    process->exit_status = status;

    spinlock_release(&process->lock);
}

struct vfs_node* process_get_cwd(struct process* process) {
    spinlock_acquire(&process->lock);
    struct vfs_node* cwd = process->cwd;
    VFS_NODE_REF(cwd);
    spinlock_release(&process->lock);
    return cwd;
}

struct vfs_node* process_get_root(struct process* process) {
    spinlock_acquire(&process->lock);
    struct vfs_node* root = process->root;
    VFS_NODE_REF(root);
    spinlock_release(&process->lock);
    return root;
}

void process_set_cwd(struct process* process, struct vfs_node* new_cwd) {
    spinlock_acquire(&process->lock);
    struct vfs_node* old_cwd = process->cwd;

    VFS_NODE_REF(new_cwd);
    process->cwd = new_cwd;

    VFS_NODE_UNREF(old_cwd);
    spinlock_release(&process->lock);
}

void process_set_root(struct process* process, struct vfs_node* new_root) {
    spinlock_acquire(&process->lock);
    struct vfs_node* old_root = process->root;

    process->root = new_root;
    VFS_NODE_REF(new_root);

    VFS_NODE_UNREF(old_root);
    spinlock_release(&process->lock);
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

struct thread* thread_create_user(struct process* process, uintptr_t entry, uintptr_t stack) {
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

    thread->registers.rip = entry;
    thread->registers.cs = 0x23;
    thread->registers.rflags = 0x202;
    thread->registers.ss = 0x1b;
    thread->registers.rsp = stack;

    process->thread_stack_top -= USER_STACK_SIZE - PAGE_SIZE_4KB; // this leaves an unmapped guard page between stacks

    thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    ((uint16_t*) thread->fpu_context)[0] = DEFAULT_FCW;
    ((uint32_t*) thread->fpu_context)[6] = DEFAULT_MXCSR;

    thread->fs_base = 0;
    thread->gs_base = 0;

    thread->tid = vector_size(process->threads);
    vector_push(process->threads, &thread);

    spinlock_release(&process->lock);
    return thread;
}

void thread_destroy(struct thread* thread) {
    if (thread->is_user) {
        pmm_free((uintptr_t) thread->fpu_context - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB));
        // user stack physical pages are already freed during vmm_context_destroy called in process_exit
    }

    pmm_free(thread->kernel_stack_paddr, KERNEL_STACK_SIZE / PAGE_SIZE_4KB);

    slab_cache_free(thread_cache, thread);
}

struct thread* thread_fork(struct process* process, struct registers* context) {
    struct thread* new_thread = slab_cache_alloc(thread_cache);
    if (unlikely(new_thread == NULL)) {
        return NULL;
    }

    new_thread->state = THREAD_READY;
    new_thread->is_user = true;
    new_thread->process = process;

    new_thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    new_thread->kernel_stack = new_thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    memcpy64((uint64_t*) &new_thread->registers, (const uint64_t*) context, sizeof(struct registers) >> 3);
    new_thread->registers.rax = 0;

    new_thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    this_cpu()->fpu_restore(new_thread->fpu_context);

    new_thread->fs_base = rdmsr(IA32_FS_BASE_MSR);
    new_thread->gs_base = rdmsr(IA32_KERNEL_GS_BASE_MSR);

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

    kernel_process = slab_cache_alloc(process_cache);
    if (unlikely(kernel_process == NULL)) {
        kpanic(NULL, false, "failed to create kernel process");
    }

    kernel_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(kernel_process->threads == NULL)) {
        kpanic(NULL, false, "failed to create kernel process threads vector");
    }

    kernel_process->pid = next_pid++;
    kernel_process->state = PROCESS_RUNNING;

    klog("[process] initialized kernel process\n");
}
