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
    int64_t status = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_exit (status: %d) on (pid: %u, tid: %u)\n",
            status, current_process->pid, current_thread->tid);

    cli();
    process_destroy(current_process, status);

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

    USER_ACCESS_BEGIN;

    klog("[syscall] running syscall_exec (path: %s, argv: 0x%x, envp: 0x%x) on (pid: %u, tid: %u)\n",
            path, (uintptr_t) argv, (uintptr_t) envp, current_process->pid, current_thread->tid);

    if (!check_user_ptr(path) || !check_user_ptr(argv) || !check_user_ptr(envp)) {
        r->rax = -EFAULT;
        return;
    }

    const char** ptr;
    const char* iter;

    ptr = argv;
    for (iter = *ptr; iter != NULL; iter = *ptr++) {
        if (!check_user_ptr(iter)) {
            r->rax = -EFAULT;
            return;
        }
    }

    ptr = envp;
    for (iter = *ptr; iter != NULL; iter = *ptr++) {
        if (!check_user_ptr(iter)) {
            r->rax = -EFAULT;
            return;
        }
    }

    struct pagemap* old_pagemap = current_process->pagemap;
    struct pagemap* new_pagemap = vmm_new_pagemap();

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    uintptr_t entry;

    if (node == NULL) {
        vmm_destroy_pagemap(new_pagemap);
        r->rax = -ENOENT;
        return;
    }

    if (!S_ISREG(node->stat.st_mode)) {
        vmm_destroy_pagemap(new_pagemap);
        r->rax = -ENOEXEC;
        return;
    }

    int res;
    if ((res = elf_load(node, new_pagemap, &entry))) {
        vmm_destroy_pagemap(new_pagemap);
        r->rax = res;
        return;
    }

    current_process->pagemap = new_pagemap;
    current_process->code_base = entry;
    current_process->brk = PROCESS_BRK_BASE;
    current_process->thread_stack_top = PROCESS_THREAD_STACK_TOP;

    for (size_t i = 0; i < MAX_FDS; i++) {
        struct file_descriptor* fd = current_process->file_descriptors[i];

        if (fd != NULL && fd->flags & O_CLOEXEC) {
            if ((res = fd_close(current_process, i)) < 0) {
                process_destroy(current_process, -1);
                r->rax = res;
                return;
            }
        }
    }

    for (size_t i = 0; i < current_process->threads->size; i++) {
        sched_thread_destroy(current_process->threads->data[i]);
    }

    vector_destroy(current_process->threads);
    current_process->threads = vector_create(sizeof(struct thread*));
    if (current_process->threads == NULL) {
        process_destroy(current_process, -1);
        r->rax = -ENOMEM;
        return;
    }

    vfs_get_pathname(node, current_process->name, sizeof(current_process->name) - 1);

    struct thread* new_thread = thread_create(current_process, entry, NULL, argv, envp, true);
    if (new_thread == NULL) {
        process_destroy(current_process, -1);
        r->rax = -ENOMEM;
        return;
    }
    sched_thread_enqueue(new_thread);

    USER_ACCESS_END;

    vmm_switch_pagemap(kernel_pagemap);
    vmm_destroy_pagemap(old_pagemap);

    this_cpu()->running_thread = NULL;
    r->rax = 0;

    sched_yield();
    __builtin_unreachable();
}

void syscall_wait(struct registers* r) {
    pid_t pid = r->rdi;
    int* status = (int*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_wait (pid: %d, status: 0x%x, flags: %d) on (pid: %u, tid: %u)\n",
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

    if (current_process->parent != NULL) {
        r->rax = current_process->parent->pid;
    } else {
        r->rax = -1;
    }
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

    klog("[syscall] running syscall_thread_create (entry: 0x%x) on (pid: %u, tid: %u)\n",
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

    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}

void syscall_sbrk(struct registers* r) {
    intptr_t size = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_sbrk (size: %d) on (pid: %u, tid: %u)\n",
            size, current_process->pid, current_thread->tid);

    uintptr_t current_brk = current_process->brk;
    current_process->brk += size;

    r->rax = current_brk;
}
