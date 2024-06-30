#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

#define STACK_SIZE 0x40000

static pid_t next_pid = 0;

struct process* process_create(struct process* old, struct pagemap* pagemap) {
    struct process* new = kmalloc(sizeof(struct process));
    if (new == NULL) {
        return NULL;
    }

    new->children = vector_create(sizeof(struct process*));
    new->threads = vector_create(sizeof(struct thread*));

    new->pid = next_pid;
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    new->state = PROCESS_RUNNING;

    // TODO: dup file descriptors
    if (old != NULL) {
        strncpy(new->name, old->name, sizeof(new->name));

        new->pagemap = old->pagemap;
        new->thread_stack_top = old->thread_stack_top;
        new->cwd = old->cwd;
        new->parent = old;

        vector_push_back(old->children, new);
    } else {
        new->pagemap = pagemap;
        new->thread_stack_top = 0x70000000000;
        new->cwd = vfs_root;
    }

    return new;
}

void process_destroy(struct process* p, int status) {
    if (p->pid < 2) {
        kpanic(NULL, "tried to exit init process");
    }

    p->exit_code = (uint8_t) status;
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

    vector_destroy(p->children);
    vector_destroy(p->threads);

    // TODO: reparent process children to init process
    vmm_destroy_pagemap(p->pagemap);
}

struct thread* thread_create(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp, bool is_user) {
    struct thread* t = kmalloc(sizeof(struct thread));
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
                    PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);

            if (!ret) {
                pmm_free(stack_paddr, STACK_SIZE / PAGE_SIZE);
                kfree(t);
                return NULL;
            }
        }

        t->stack = stack_paddr;
        t->ctx.rsp = p->thread_stack_top;
        p->thread_stack_top -= STACK_SIZE;

        t->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        t->page_fault_stack += STACK_SIZE + HIGH_VMA;

        t->fpu_storage = (void*) (pmm_allocz(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE)) + HIGH_VMA);

        this_cpu()->fpu_restore(t->fpu_storage);
        uint16_t default_fcw = 0x33f;
        __asm__ volatile ("fldcw %0" :: "m" (default_fcw) : "memory");
        this_cpu()->fpu_save(t->fpu_storage);

        t->fs_base = 0;
        t->gs_base = 0;

        if (p->threads->size == 0 && argv != NULL && envp != NULL) {
            uintptr_t* stack = (uintptr_t*) (stack_paddr + STACK_SIZE + HIGH_VMA);
            void* stack_top = stack;

            int envp_len;
            for (envp_len = 0; envp[envp_len] != NULL; envp_len++) {
                size_t length = strlen(envp[envp_len]);
                stack = (uintptr_t*) ((uintptr_t) stack - length - 1);
                memcpy(stack, envp[envp_len], length);
            }

            int argv_len;
            for (argv_len = 0; argv[argv_len] != NULL; argv_len++) {
                size_t length = strlen(argv[argv_len]);
                stack = (uintptr_t*) ((uintptr_t) stack - length - 1);
                memcpy(stack, argv[argv_len], length);
            }

            stack = (uintptr_t*) ALIGN_DOWN((uintptr_t) stack, 16);
            if ((argv_len + envp_len + 1) & 1) {
                stack--;
            }

            uintptr_t old_rsp = t->ctx.rsp;

            *(--stack) = 0;
            stack -= envp_len;
            for (int i = 0; i < envp_len; i++) {
                old_rsp -= strlen(envp[i]) + 1;
                stack[i] = old_rsp;
            }

            *(--stack) = 0;
            stack -= argv_len;
            for (int i = 0; i < argv_len; i++) {
                old_rsp -= strlen(argv[i]) + 1;
                stack[i] = old_rsp;
            }

            *(--stack) = argv_len;

            t->ctx.rsp -= (uintptr_t) stack_top - (uintptr_t) stack;
        }
    } else {
        t->timeslice = 5000;

        t->ctx.cs = 0x08;
        t->ctx.ss = 0x10;

        t->page_fault_stack = t->kernel_stack;
        t->stack = t->kernel_stack;
        t->ctx.rsp = t->stack;

        t->fs_base = rdmsr(MSR_FS_BASE);
        t->gs_base = rdmsr(MSR_KERNEL_GS);
    }

    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;
    t->ctx.rdi = (uint64_t) arg;

    t->tid = p->threads->size;
    vector_push_back(p->threads, t);

    return t;
}

struct thread* thread_fork(struct process* forked, struct thread* old_thread) {
    struct thread* new_thread = kmalloc(sizeof(struct thread));
    if (new_thread == NULL) {
        return NULL;
    }

    new_thread->state = THREAD_READY_TO_RUN;
    new_thread->process = forked;
    new_thread->lock = (spinlock_t) {0};
    new_thread->timeslice = old_thread->timeslice;
    
    new_thread->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    new_thread->kernel_stack += STACK_SIZE + HIGH_VMA;

    new_thread->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    new_thread->page_fault_stack += STACK_SIZE + HIGH_VMA;

    new_thread->ctx = old_thread->ctx;
    new_thread->ctx.rax = 0;
    new_thread->ctx.rbx = 0;

    new_thread->stack = new_thread->ctx.rsp;

    new_thread->fpu_storage = (void*) (pmm_allocz(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE)) + HIGH_VMA);
    memcpy(new_thread->fpu_storage, old_thread->fpu_storage, this_cpu()->fpu_storage_size);

    new_thread->fs_base = old_thread->fs_base;
    new_thread->gs_base = old_thread->gs_base;

    new_thread->tid = forked->threads->size;
    vector_push_back(forked->threads, new_thread);

    return new_thread;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);

    if (t->ctx.cs & 0x3) {
        pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free((uintptr_t) t->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }

    kfree(t);
}
