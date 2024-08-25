#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <errno.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/panic.h>
#include <utils/string.h>
#include <utils/vector.h>

#define STACK_SIZE 0x40000

static struct cache* process_cache;
static struct cache* dead_process_cache;
static struct cache* thread_cache;

// TODO: use actual tree data structure for processes
static vector_t* running_processes;
static vector_t* dead_processes;
static pid_t next_pid;
static spinlock_t process_list_lock = {0};

static bool create_std_file_descriptors(struct process* p, const char* console_path) {
    struct vfs_node* console_node = vfs_get_node(p->cwd, console_path);
    if (console_node == NULL) {
        return false;
    }

    struct file_descriptor* stdin = fd_create(console_node, O_RDONLY);
    if (stdin == NULL) {
        return false;
    }

    if (fd_alloc_fdnum(p, stdin) < 0) {
        return false;
    }

    struct file_descriptor* stdout = fd_create(console_node, O_WRONLY);
    if (stdout == NULL) {
        return false;
    }

    if (fd_alloc_fdnum(p, stdout) < 0) {
        return false;
    }

    struct file_descriptor* stderr = fd_create(console_node, O_WRONLY);
    if (stderr == NULL) {
        return false;
    }

    if (fd_alloc_fdnum(p, stderr) < 0) {
        return false;
    }

    return true;
}

struct process* process_create(struct process* old, struct pagemap* pagemap) {
    struct process* new = cache_alloc_object(process_cache);
    if (new == NULL) {
        return NULL;
    }

    new->state = PROCESS_RUNNING;

    new->children = vector_create(sizeof(struct process*));
    if (new->children == NULL) {
        goto error;
    }

    new->threads = vector_create(sizeof(struct thread*));
    if (new->threads == NULL) {
        goto error;
    }

    if (old != NULL) {
        strncpy(new->name, old->name, sizeof(new->name));

        new->pagemap = vmm_fork_pagemap(old->pagemap);
        if (new->pagemap == NULL) {
            goto error;
        }

        new->brk = old->brk;
        new->thread_stack_top = old->thread_stack_top;
        new->cwd = old->cwd;
        new->parent = old;

        for (int i = 0; i < MAX_FDS; i++) {
            if (old->file_descriptors[i] == NULL) {
                continue;
            }

            if (!fd_dup(old, i, new, i)) {
                goto error;
            }
        }

        vector_push_back(old->children, new);
    } else {
        new->pagemap = pagemap;
        new->brk = new->brk_next_unallocated_page_begin = PROCESS_BRK_BASE;
        new->thread_stack_top = PROCESS_THREAD_STACK_TOP;
        new->cwd = vfs_root;
    }

    new->pid = next_pid;
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    spinlock_acquire(&process_list_lock);
    vector_push_back(running_processes, new);
    spinlock_release(&process_list_lock);

    goto end;

error:
    if (new->children != NULL) {
        vector_destroy(new->children);
    }
    if (new->threads != NULL) {
        vector_destroy(new->threads);
    }
    if (new->pagemap != NULL) {
        vmm_destroy_pagemap(new->pagemap);
    }

    cache_free_object(process_cache, new);
    new = NULL;
end:
    return new;
}

bool process_create_init(void) {
    char* init_path = cmdline_get("init");
    if (!init_path) {
        init_path = "/bin/init";
    }

    klog("[process] creating init process (pid = 1) using %s\n", init_path);

    struct vfs_node* init_node = vfs_get_node(vfs_root, init_path);
    if (init_node == NULL) {
        return false;
    }

    struct pagemap* init_pagemap = vmm_new_pagemap();

    uintptr_t entry;
    if (elf_load(init_node, init_pagemap, &entry) < 0) {
        vmm_destroy_pagemap(init_pagemap);
        return false;
    }

    const char* argv[] = { init_path, NULL };
    const char* envp[] = { NULL };

    struct process* init_process = process_create(NULL, init_pagemap);
    if (init_process == NULL) {
        vmm_destroy_pagemap(init_pagemap);
        return false;
    }

    if (!create_std_file_descriptors(init_process, "/dev/tty0")) {
        process_exit(init_process, -1);
        return false;
    };

    init_process->code_base = entry;
    vfs_get_pathname(vfs_root, init_process->name, sizeof(init_process->name) - 1);

    struct thread* init_thread = thread_create(init_process, entry, NULL, argv, envp, true);
    if (init_thread == NULL) {
        process_exit(init_process, -1);
        return false;
    }

    sched_thread_enqueue(init_thread);
    return true;
}

void process_destroy(struct process* p) {
    if (p->parent) {
        vector_remove_by_value(p->parent->children, p);
    }

    for (int i = 0; i < MAX_FDS; i++) {
        fd_close(p, i);
    }

    /* reparent the now dead process' children to init (pid=1) */
    struct process* init = vector_get(running_processes, 1);
    for (size_t i = 0; i < p->children->size; i++) {
        vector_push_back(init->children, vector_get(p->children, i));
    }

    vector_destroy(p->children);
    vector_destroy(p->threads);

    vmm_destroy_pagemap(p->pagemap);

    struct dead_process* dp = cache_alloc_object(dead_process_cache);
    dp->pid = p->pid;
    dp->ppid = p->parent != NULL ? p->parent->pid : 0;
    dp->status = p->status;

    spinlock_acquire(&process_list_lock);
    vector_remove_by_value(running_processes, p);
    vector_push_back(dead_processes, dp);
    spinlock_release(&process_list_lock);

    cache_free_object(process_cache, p);
}

void process_exit(struct process* p, int status) {
    if (p->pid < 2) {
        kpanic(NULL, true, "tried to exit init process");
    }

    /*
     * This is all that *needs* to be done to get a process to just stop running.
     * All of the actual process teardown is done in process_destroy, which is run lazily by the scheduler.
     */
    p->state = PROCESS_ZOMBIE;
    p->status = status;

    for (size_t i = 0; i < p->threads->size; i++) {
        sched_thread_dequeue(p->threads->data[i]);
    }
}

void* process_sbrk(struct process* p, intptr_t size) {
    uintptr_t old_brk = p->brk;

    if (size > 0) {
        size_t remaining = p->brk_next_unallocated_page_begin - p->brk;

        if ((unsigned) size > remaining) {
            size_t bytes_needed = size - remaining;
            size_t page_count = ((bytes_needed - 1) / PAGE_SIZE) + 1;

            uintptr_t paddr = pmm_alloc(page_count);
            if (paddr == 0) {
                return (void*) -1;
            }

            for (size_t i = 0; i < page_count; i++) {
                if (!vmm_map_page(p->pagemap, p->brk_next_unallocated_page_begin + (i * PAGE_SIZE),
                            paddr + (i * PAGE_SIZE), PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX)) {
                    return (void*) -1;
                }
            }

            p->brk_next_unallocated_page_begin += page_count * PAGE_SIZE;
        }
    } else if (size < 0) {
        uintptr_t current_page_start = p->brk_next_unallocated_page_begin - PAGE_SIZE;
        size_t remaining = p->brk - current_page_start;

        if ((unsigned) -size > remaining) {
            size_t page_count = (((-size - remaining) - 1) / PAGE_SIZE) + 1;
            for (size_t i = 0; i < page_count; i++) {
                if (p->brk_next_unallocated_page_begin - PAGE_SIZE >= PROCESS_BRK_BASE) {
                    p->brk_next_unallocated_page_begin -= PAGE_SIZE;
                    vmm_unmap_page(p->pagemap, p->brk_next_unallocated_page_begin);
                }
            }
        }
    }

    p->brk += size;
    return (void*) old_brk;
}

pid_t process_wait(struct process* p, pid_t pid, int* status, int flags) {
    p->state = PROCESS_WAITING;

    struct process* child = NULL;

    if (pid == -1) {
        while (true) {
            if (p->children->size == 0) {
                return -ECHILD;
            }

            struct process* iter;
            for (size_t i = 0; i < p->children->size; i++) {
                iter = p->children->data[i];
                if (iter->state == PROCESS_ZOMBIE) {
                    child = iter;
                    break;
                }
            }

            if (flags & WNOHANG) {
                return -EAGAIN;
            }

            sched_yield();
        }
    } else if (pid > 0) {
        struct process* iter;
        for (size_t i = 0; i < p->children->size; i++) {
            iter = p->children->data[i];
            if (iter->pid == pid) {
                child = iter;
                break;
            }
        }

        if (child == NULL) {
            return -ECHILD;
        }

        while (child->state != PROCESS_ZOMBIE) {
            if (flags & WNOHANG) {
                return -EAGAIN;
            }
            sched_yield();
        }
    } else {
        return -EINVAL;
    }

    p->state = PROCESS_RUNNING;

    if (status) {
        *status = child->status;
    }

    return child->pid;
}

struct thread* thread_create(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp, bool is_user) {
    struct thread* t = cache_alloc_object(thread_cache);
    if (t == NULL) {
        return NULL;
    }

    t->state = THREAD_READY_TO_RUN;
    t->process = p;
    t->is_user = is_user;
    t->lock = (spinlock_t) {0};

    uintptr_t user_stack_paddr = 0;

    t->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (t->kernel_stack == 0) {
        goto error;
    }
    t->kernel_stack += STACK_SIZE + HIGH_VMA;

    if (is_user) {
        t->timeslice = 20000;

        t->ctx.cs = 0x23;
        t->ctx.ss = 0x1b;

        user_stack_paddr = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        if (user_stack_paddr == 0) {
            goto error;
        }

        for (size_t i = 0; i < STACK_SIZE / PAGE_SIZE; i++) {
            if (!vmm_map_page(p->pagemap,
                        (p->thread_stack_top - STACK_SIZE) + (i * PAGE_SIZE),
                        user_stack_paddr + (i * PAGE_SIZE),
                        PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX)) {
                goto error;
            }
        }

        t->ctx.rsp = p->thread_stack_top;
        p->thread_stack_top -= STACK_SIZE - PAGE_SIZE;

        t->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        if (t->page_fault_stack == 0) {
            goto error;
        }
        t->page_fault_stack += STACK_SIZE + HIGH_VMA;

        t->fpu_storage = (void*) pmm_allocz(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
        if (t->fpu_storage == NULL) {
            goto error;
        }
        t->fpu_storage = (void*) ((uintptr_t) t->fpu_storage + HIGH_VMA);

        this_cpu()->fpu_restore(t->fpu_storage);
        uint16_t default_fcw = 0x33f;
        __asm__ volatile ("fldcw %0" :: "m" (default_fcw) : "memory");
        uint32_t default_mxcsr = 0x1f80;
        __asm__ volatile ("ldmxcsr %0" :: "m" (default_mxcsr) : "memory");
        this_cpu()->fpu_save(t->fpu_storage);

        t->fs_base = 0;
        t->gs_base = 0;

        if (p->threads->size == 0 && argv != NULL && envp != NULL) {
            void* stack_top = (void*) (user_stack_paddr + STACK_SIZE + HIGH_VMA);
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

            uintptr_t old_rsp = t->ctx.rsp;

            *(--stack) = 0;
            stack -= envp_len;
            int i;
            for (i = 0; i < envp_len; i++) {
                old_rsp -= strlen(envp[i]) + 1;
                stack[i] = old_rsp;
            }

            t->ctx.rdx = t->ctx.rsp - ((uintptr_t) stack_top - (uintptr_t) stack); // envp

            *(--stack) = 0;
            stack -= argv_len;
            for (i = 0; i < argv_len; i++) {
                old_rsp -= strlen(argv[i]) + 1;
                stack[i] = old_rsp;
            }

            t->ctx.rsi = t->ctx.rsp - ((uintptr_t) stack_top - (uintptr_t) stack); // argv
            t->ctx.rdi = argv_len;  // argc

            t->ctx.rsp -= (uintptr_t) stack_top - (uintptr_t) stack;
            t->user_stack = t->ctx.rsp;
        }
    } else {
        t->timeslice = 5000;

        t->ctx.rdi = (uint64_t) arg;

        t->ctx.cs = 0x08;
        t->ctx.ss = 0x10;

        t->page_fault_stack = t->ctx.rsp = t->kernel_stack;

        t->fs_base = rdmsr(IA32_FS_BASE_MSR);
        t->gs_base = rdmsr(IA32_GS_BASE_MSR);
    }

    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;

    t->tid = p->threads->size;
    vector_push_back(p->threads, t);

    goto end;

error:
    if (t->kernel_stack != 0) {
        pmm_free(t->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    }
    if (is_user) {
        if (t->page_fault_stack != 0) {
            pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        }
        if (user_stack_paddr != 0) {
            pmm_free(user_stack_paddr - STACK_SIZE, STACK_SIZE / PAGE_SIZE);
        }
    }
    cache_free_object(thread_cache, t);
    t = NULL;
end:
    return t;
}

struct thread* thread_fork(struct process* forked, struct thread* old_thread) {
    struct thread* new_thread = cache_alloc_object(thread_cache);
    if (new_thread == NULL) {
        return NULL;
    }

    new_thread->state = THREAD_READY_TO_RUN;
    new_thread->process = forked;
    new_thread->lock = (spinlock_t) {0};
    new_thread->timeslice = old_thread->timeslice;

    new_thread->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (new_thread->kernel_stack == 0) {
        goto error;
    }
    new_thread->kernel_stack += STACK_SIZE + HIGH_VMA;

    new_thread->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (new_thread->page_fault_stack == 0) {
        goto error;
    }
    new_thread->page_fault_stack += STACK_SIZE + HIGH_VMA;

    new_thread->user_stack = old_thread->user_stack;

    new_thread->ctx = old_thread->ctx;
    new_thread->ctx.rax = 0;

    new_thread->fpu_storage = (void*) pmm_alloc(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    if (new_thread->fpu_storage == NULL) {
        goto error;
    }
    new_thread->fpu_storage = (void*) ((uintptr_t) new_thread->fpu_storage + HIGH_VMA);
    memcpy(new_thread->fpu_storage, old_thread->fpu_storage, this_cpu()->fpu_storage_size);

    new_thread->fs_base = old_thread->fs_base;
    new_thread->gs_base = old_thread->gs_base;

    new_thread->tid = forked->threads->size;
    vector_push_back(forked->threads, new_thread);

    goto end;

error:
    if (new_thread->kernel_stack != 0) {
        pmm_free(new_thread->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    }
    if (new_thread->page_fault_stack != 0) {
        pmm_free(new_thread->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    }
    cache_free_object(thread_cache, new_thread);
    new_thread = NULL;
end:
    return new_thread;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);

    if (t->is_user) {
        pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free((uintptr_t) t->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }

    vector_remove_by_value(t->process->threads, t);
    cache_free_object(thread_cache, t);
}

void process_init(void) {
    process_cache = slab_cache_create("process cache", sizeof(struct process));
    if (process_cache == NULL) {
        kpanic(NULL, false, "failed to initialize object cache for process structures");
    }

    dead_process_cache = slab_cache_create("dead process cache", sizeof(struct dead_process));
    if (dead_process_cache == NULL) {
        kpanic(NULL, false, "failed to initialize object cache for dead process metadata structures");
    }

    thread_cache = slab_cache_create("thread cache", sizeof(struct thread));
    if (thread_cache == NULL) {
        kpanic(NULL, false, "failed to initialize object cache for thread structures");
    }

    running_processes = vector_create(sizeof(struct process*));
    if (running_processes == NULL) {
        kpanic(NULL, false, "failed to create running process vector");
    }

    dead_processes = vector_create(sizeof(struct dead_process*));
    if (dead_processes == NULL) {
        kpanic(NULL, false, "failed to create dead process vector");
    }
}
