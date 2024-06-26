#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/sched.h>
#include <types.h>
#include <utils/log.h>

// TODO: refactor file descriptor management code into its own file/api

void syscall_exit(struct registers* r) {
    uint8_t status = r->rdi;

    struct process* current_process = this_cpu()->running_thread->process;

    if (current_process->pid < 2) {
        kpanic(NULL, "tried to exit init process");
    }

    vmm_switch_pagemap(&kernel_pagemap);
    this_cpu()->running_thread->process = kernel_process;

    cli();

    spinlock_acquire(&current_process->fd_lock);

    struct file_descriptor* fd;
    for (size_t i = 0; i < MAX_FDS; i++) {
        fd = current_process->file_descriptors[i];

        if (fd != NULL) {
            fd->node->refcount--;

            if (fd->refcount-- == 1) {
                kfree(fd);
            }

            current_process->file_descriptors[i] = NULL;
        }
    }

    spinlock_release(&current_process->fd_lock);

    for (size_t i = 0; i < current_process->threads->size; i++) {
        sched_thread_destroy(current_process->threads->data[i]);
    }

    if (current_process->parent) {
        vector_remove_by_value(current_process->parent->children, current_process);
    }

    // TODO: reparent process children to init process

    current_process->exit_code = status;

    process_destroy(current_process);
    sched_yield();
}

// TODO: implement fork
void syscall_fork(struct registers* r) {
    r->rax = (uint64_t) -1;
}

// TODO: shebang support
void syscall_exec(struct registers* r) {
    const char* path = (char*) r->rdi;
    const char** argv = (const char**) r->rsi;
    const char** envp = (const char**) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct pagemap* new_pagemap = vmm_new_pagemap();
    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    uintptr_t entry;

    if (node == NULL || !elf_load(node, new_pagemap, 0, &entry)) {
        r->rax = (uint64_t) -1;
        return;
    }

    // TODO: destroy old pagemap and kill old threads

    current_process->pagemap = new_pagemap;
    current_process->thread_stack_top = 0x70000000000;

    spinlock_acquire(&current_process->fd_lock);

    struct file_descriptor* fd;
    for (size_t i = 0; i < MAX_FDS; i++) {
        fd = current_process->file_descriptors[i];

        if (fd != NULL && fd->flags & O_CLOEXEC) {
            fd->node->refcount--;

            if (fd->refcount-- == 1) {
                kfree(fd);
            }

            current_process->file_descriptors[i] = NULL;
        }
    }

    spinlock_release(&current_process->fd_lock);

    for (size_t i = 0; i < current_process->threads->size; i++) {
        sched_thread_destroy(current_process->threads->data[i]);
    }

    vector_destroy(current_process->threads);
    current_process->threads = vector_create(sizeof(struct thread*));

    vfs_get_pathname(node, current_process->name, sizeof(current_process->name) - 1);

    struct thread* new_thread = thread_create_user(current_process, entry, NULL, argv, envp);
    if (new_thread == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    vmm_switch_pagemap(&kernel_pagemap);

    sched_thread_enqueue(new_thread);
    sched_yield();
}

void syscall_yield(struct registers* r) {
    (void) r;
    sched_yield();
}

void syscall_getpid(struct registers* r) {
    r->rax = this_cpu()->running_thread->process->pid;
}

void syscall_gettid(struct registers* r) {
    r->rax = this_cpu()->running_thread->tid;
}

void syscall_thread_create(struct registers* r) {
    uintptr_t entry = (uintptr_t) r->rdi;

    struct thread* new_thread = thread_create_user(this_cpu()->running_thread->process, entry, NULL, NULL, NULL);
    sched_thread_enqueue(new_thread);

    r->rax = (uint64_t) new_thread->tid;
}

void syscall_thread_exit(struct registers* r) {
    (void) r;
    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}
