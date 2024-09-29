#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <errno.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/sched.h>
#include <types.h>
#include <utils/log.h>
#include <utils/user_access.h>

__attribute__((noreturn)) void syscall_exit(struct registers* r) {
    int status = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_exit (status: %d) on (pid: %u, tid: %u)\n",
            status, current_process->pid, current_thread->tid);

    process_exit(current_process, status);

    this_cpu()->running_thread = NULL;
    sched_yield();
    __builtin_unreachable();
}

void syscall_fork(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_fork on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    struct process* new_process = process_create(current_process, NULL);
    if (new_process == NULL) {
        r->rax = -ENOMEM;
        return;
    }

    struct thread* new_thread = thread_fork(new_process, current_thread);
    if (new_thread == NULL) {
        r->rax = -ENOMEM;
        return;
    }

    r->rax = new_process->pid;
    sched_thread_enqueue(new_thread);
}

// TODO: shebang support
void syscall_exec(struct registers* r) {
    const char* path = (char*) r->rdi;
    const char** argv = (const char**) r->rsi;
    const char** envp = (const char**) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

    USER_ACCESS_BEGIN;

    klog("[syscall] running syscall_exec (path: %s, argv: 0x%p, envp: 0x%p) on (pid: %u, tid: %u)\n",
            path, (uintptr_t) argv, (uintptr_t) envp, current_process->pid, current_thread->tid);

    struct pagemap* old_pagemap = current_process->pagemap;
    struct pagemap* new_pagemap = vmm_new_pagemap();
    if (new_pagemap == NULL) {
        ret = -ENOMEM;
        goto error;
    }

    if (!check_user_ptr(path) || !check_user_ptr(argv) || !check_user_ptr(envp)) {
        ret = -EFAULT;
        goto error;
    }

    const char** ptr;
    const char* iter;

    ptr = argv;
    for (iter = *ptr; iter != NULL; iter = *ptr++) {
        if (!check_user_ptr(iter)) {
            ret = -EFAULT;
            goto error;
        }
    }

    ptr = envp;
    for (iter = *ptr; iter != NULL; iter = *ptr++) {
        if (!check_user_ptr(iter)) {
            ret = -EFAULT;
            goto error;
        }
    }

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node == NULL) {
        ret = -ENOENT;
        goto error;
    }

    if (!S_ISREG(node->stat.st_mode)) {
        ret = -ENOEXEC;
        goto error;
    }

    uintptr_t entry;
    if ((ret = elf_load(node, new_pagemap, &entry)) < 0) {
        goto error;
    }

    current_process->pagemap = new_pagemap;
    current_process->code_base = entry;
    current_process->brk = current_process->brk_next_unallocated_page_begin = PROCESS_BRK_BASE;
    current_process->thread_stack_top = PROCESS_THREAD_STACK_TOP;

    for (size_t i = 0; i < MAX_FDS; i++) {
        struct file_descriptor* fd = current_process->file_descriptors[i];

        if (fd != NULL && fd->flags & O_CLOEXEC) {
            if ((ret = fd_close(current_process, i)) < 0) {
                goto error;
            }
        }
    }

    cli();
    for (size_t i = 0; i < current_process->threads->size; i++) {
        sched_thread_dequeue(current_process->threads->data[i]);
    }

    vector_destroy(current_process->threads);
    current_process->threads = vector_create(sizeof(struct thread*));
    if (current_process->threads == NULL) {
        ret = -ENOMEM;
        goto error;
    }

    struct thread* new_thread = thread_create(current_process, entry, NULL, argv, envp, true);
    if (new_thread == NULL) {
        ret = -ENOMEM;
        goto error;
    }

    sched_thread_enqueue(new_thread);
    sti();

    USER_ACCESS_END;

    vfs_get_pathname(node, current_process->name, sizeof(current_process->name) - 1);

    vmm_switch_pagemap(kernel_pagemap);
    vmm_destroy_pagemap(old_pagemap);

    this_cpu()->running_thread = NULL;
    r->rax = 0;

    sched_yield();
    __builtin_unreachable();

error:
    if (current_process->pagemap == old_pagemap) {
        if (new_pagemap != NULL) {
            vmm_destroy_pagemap(new_pagemap);
        }
    } else {
        process_exit(current_process, -1);
    }

    USER_ACCESS_END;
    r->rax = ret;
}

void syscall_wait(struct registers* r) {
    pid_t pid = r->rdi;
    int* status = (int*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_wait (pid: %d, status: 0x%p, flags: %d) on (pid: %u, tid: %u)\n",
            pid, (uintptr_t) status, flags, current_process->pid, current_thread->tid);

    if (status != NULL) {
        if (!check_user_ptr(status)) {
            r->rax = -EFAULT;
            return;
        }
    }

    USER_ACCESS_BEGIN;
    r->rax = process_wait(current_process, pid, status, flags);
    USER_ACCESS_END;
}

void syscall_yield(struct registers* r) {
    (void) r;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_yield on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    sched_yield();
}

void syscall_getpid(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getpid on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    r->rax = current_process->pid;
}

void syscall_getppid(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getppid on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    r->rax = current_process->parent != NULL ? current_process->parent->pid : 0;
}

void syscall_gettid(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_gettid on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    r->rax = current_thread->tid;
}

void syscall_thread_create(struct registers* r) {
    uintptr_t entry = (uintptr_t) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_thread_create (entry: 0x%p) on (pid: %u, tid: %u)\n",
            entry, current_process->pid, current_thread->tid);

    if (!check_user_ptr((void*) entry)) {
        r->rax = -EFAULT;
        return;
    }

    struct thread* new_thread = thread_create(current_process, entry, NULL, NULL, NULL, true);
    sched_thread_enqueue(new_thread);

    r->rax = (uint64_t) new_thread->tid;
}

void syscall_thread_exit(struct registers* r) {
    (void) r;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_thread_exit on (pid: %u, tid: %u)\n",
            current_process->pid, current_thread->tid);

    sched_thread_dequeue(this_cpu()->running_thread);
    sched_yield();
}

void syscall_sbrk(struct registers* r) {
    intptr_t size = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_sbrk (size: %ld) on (pid: %u, tid: %u)\n",
            size, current_process->pid, current_thread->tid);

    r->rax = (uint64_t) process_sbrk(current_process, size);
}
