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
#include <utils/string.h>
#include <utils/vector.h>

#define STACK_SIZE 0x40000

static struct cache* process_cache;
static struct cache* thread_cache;

// TODO: use actual tree data structure for processes
static vector_t* running_processes;
static vector_t* dead_processes;
static pid_t next_pid;
static spinlock_t process_lock = {0};

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
        cache_free_object(process_cache, new);
        spinlock_release(&process_lock);
        return NULL;
    }

    new->threads = vector_create(sizeof(struct thread*));
    if (new->threads == NULL) {
        kfree(new->children);
        cache_free_object(process_cache, new);

        spinlock_release(&process_lock);
        return NULL;
    }

    if (old != NULL) {
        strncpy(new->name, old->name, sizeof(new->name));

        new->pagemap = vmm_fork_pagemap(old->pagemap);
        if (new->pagemap == NULL) {
            kfree(new->children);
            kfree(new->threads);
            cache_free_object(process_cache, new);

            spinlock_release(&process_lock);
            return NULL;
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
                vmm_destroy_pagemap(new->pagemap);
                kfree(new->children);
                kfree(new->threads);
                cache_free_object(process_cache, new);

                spinlock_release(&process_lock);
                return NULL;
            }
        }

        vector_push_back(old->children, new);
    } else {
        new->pagemap = pagemap;
        new->brk = PROCESS_BRK_BASE;
        new->thread_stack_top = PROCESS_THREAD_STACK_TOP;
        new->cwd = vfs_root;
    }

    new->pid = next_pid;
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    spinlock_acquire(&process_lock);
    vector_push_back(running_processes, new);
    spinlock_release(&process_lock);
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

    const char* argv[] = { init_path };
    const char* envp[] = { NULL };

    struct process* init_process = process_create(NULL, init_pagemap);
    if (init_process == NULL) {
        vmm_destroy_pagemap(init_pagemap);
        return false;
    }

    if (!create_std_file_descriptors(init_process, "/dev/tty0")) {
        process_destroy(init_process, -1);
        return false;
    };

    init_process->code_base = entry;
    vfs_get_pathname(vfs_root, init_process->name, sizeof(init_process->name) - 1);

    struct thread* init_thread = thread_create(init_process, entry, NULL, argv, envp, true);
    if (init_thread == NULL) {
        process_destroy(init_process, -1);
        return false;
    }

    sched_thread_enqueue(init_thread);
    return true;
}

void process_destroy(struct process* p, int status) {
    if (p->pid < 2) {
        kpanic(NULL, "tried to exit init process");
    }

    spinlock_acquire(&process_lock);

    p->exit_code = (uint8_t) (status & 0xff);
    p->state = PROCESS_ZOMBIE;

    for (size_t i = 0; i < p->threads->size; i++) {
        sched_thread_destroy(p->threads->data[i]);
    }

    if (p->parent) {
        vector_remove_by_value(p->parent->children, p);
    }

    for (int i = 0; i < MAX_FDS; i++) {
        fd_close(p, i);
    }

    struct process* init = vector_get(running_processes, 1);
    for (size_t i = 0; i < p->children->size; i++) {
        vector_push_back(init->children, vector_get(p->children, i));
    }

    vector_destroy(p->children);
    vector_destroy(p->threads);

    vmm_destroy_pagemap(p->pagemap);

    vector_remove_by_value(running_processes, p);
    vector_push_back(dead_processes, p);

    spinlock_release(&process_lock);
}

pid_t process_wait(struct process* p, pid_t pid, int* status, int flags) {
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

    if (status) {
        *status = child->exit_code;
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
    t->lock = (spinlock_t) {0};

    t->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    t->kernel_stack += STACK_SIZE + HIGH_VMA;

    if (is_user) {
        t->timeslice = 20000;

        t->ctx.cs = 0x23;
        t->ctx.ss = 0x1b;

        uintptr_t stack_paddr = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        if (!stack_paddr) {
            kfree(t);
            return NULL;
        }

        for (size_t i = 0; i < ALIGN_UP(STACK_SIZE, PAGE_SIZE) / PAGE_SIZE + 1; i++) {
            bool ret = vmm_map_page(p->pagemap,
                    (p->thread_stack_top - STACK_SIZE) + (i * PAGE_SIZE),
                    stack_paddr + (i * PAGE_SIZE),
                    PTE_PRESENT | PTE_WRITABLE | PTE_USER);

            if (!ret) {
                pmm_free(stack_paddr, STACK_SIZE / PAGE_SIZE);
                kfree(t);
                return NULL;
            }
        }

        t->ctx.rsp = p->thread_stack_top;
        p->thread_stack_top -= STACK_SIZE - PAGE_SIZE;

        t->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        t->page_fault_stack += STACK_SIZE + HIGH_VMA;

        t->fpu_storage = (void*) (pmm_allocz(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE)) + HIGH_VMA);

        this_cpu()->fpu_restore(t->fpu_storage);
        uint16_t default_fcw = 0x33f;
        __asm__ volatile ("fldcw %0" :: "m" (default_fcw) : "memory");
        uint32_t default_mxcsr = 0x1f80;
        __asm__ volatile ("ldmxcsr %0" :: "m" (default_mxcsr) : "memory");
        this_cpu()->fpu_save(t->fpu_storage);

        t->fs_base = 0;
        t->gs_base = 0;

        if (p->threads->size == 0 && argv != NULL && envp != NULL) {
            void* stack_top = (void*) (stack_paddr + STACK_SIZE + HIGH_VMA);
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
            t->stack = t->ctx.rsp;
        }
    } else {
        t->timeslice = 5000;

        t->ctx.rdi = (uint64_t) arg;

        t->ctx.cs = 0x08;
        t->ctx.ss = 0x10;

        t->page_fault_stack = t->kernel_stack;
        t->stack = t->ctx.rsp = t->kernel_stack;

        t->fs_base = rdmsr(IA32_FS_BASE_MSR);
        t->gs_base = rdmsr(IA32_GS_BASE_MSR);
    }

    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;

    t->tid = p->threads->size;
    vector_push_back(p->threads, t);

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
    if (!new_thread->kernel_stack) {
        cache_free_object(thread_cache, new_thread);
        return NULL;
    }
    new_thread->kernel_stack += STACK_SIZE + HIGH_VMA;

    new_thread->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (!new_thread->page_fault_stack) {
        pmm_free(new_thread->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        cache_free_object(thread_cache, new_thread);
        return NULL;
    }
    new_thread->page_fault_stack += STACK_SIZE + HIGH_VMA;

    new_thread->stack = old_thread->stack;

    new_thread->ctx = old_thread->ctx;
    new_thread->ctx.rax = 0;

    new_thread->fpu_storage = (void*) pmm_alloc(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    if (new_thread->fpu_storage == NULL) {
        pmm_free(new_thread->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free(new_thread->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        cache_free_object(thread_cache, new_thread);
        return NULL;
    }

    new_thread->fpu_storage = (void*) ((uintptr_t) new_thread->fpu_storage + HIGH_VMA);
    memcpy(new_thread->fpu_storage, old_thread->fpu_storage, this_cpu()->fpu_storage_size);

    new_thread->fs_base = old_thread->fs_base;
    new_thread->gs_base = old_thread->gs_base;

    new_thread->tid = forked->threads->size;
    vector_push_back(forked->threads, new_thread);

    return new_thread;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);

    if (t->ctx.cs & 3) {
        pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free((uintptr_t) t->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }

    cache_free_object(thread_cache, t);
}

void process_init(void) {
    process_cache = slab_cache_create("process cache", sizeof(struct process));
    if (process_cache == NULL) {
        kpanic(NULL, "failed to initialize object cache for process structures");
    }

    thread_cache = slab_cache_create("thread cache", sizeof(struct thread));
    if (thread_cache == NULL) {
        kpanic(NULL, "failed to initialize object cache for thread structures");
    }

    running_processes = vector_create(sizeof(struct process*));
    if (running_processes == NULL) {
        kpanic(NULL, "failed to create running process vector");
    }

    dead_processes = vector_create(sizeof(struct process*));
    if (dead_processes == NULL) {
        kpanic(NULL, "failed to create dead process vector");
    }
}
