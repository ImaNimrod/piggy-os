#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/sched.h>
#include <types.h>
#include <utils/log.h>

void syscall_exit(struct registers* r) {
    int status = r->rdi;

    struct process* current_process = this_cpu()->running_thread->process;

    cli();
    process_destroy(current_process, status);

    this_cpu()->running_thread = NULL;
    sched_yield();
}

void syscall_fork(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct process* new_process = process_create(current_process, NULL);
    if (new_process == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct thread* new_thread = thread_fork(new_process, current_thread);
    if (new_thread == NULL) {
        r->rax = (uint64_t) -1;
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

    struct pagemap* old_pagemap = current_process->pagemap;
    struct pagemap* new_pagemap = vmm_new_pagemap();

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    uintptr_t entry;

    if (node == NULL || !elf_load(node, new_pagemap, 0, &entry)) {
        vmm_destroy_pagemap(new_pagemap);
        r->rax = (uint64_t) -1;
        return;
    }

    current_process->pagemap = new_pagemap;
    current_process->thread_stack_top = 0x70000000000;

    for (size_t i = 0; i < MAX_FDS; i++) {
        struct file_descriptor* fd = current_process->file_descriptors[i];
        if (fd != NULL && fd->flags & O_CLOEXEC) {
            fd_close(current_process, i);
        }
    }

    for (size_t i = 0; i < current_process->threads->size; i++) {
        sched_thread_destroy(current_process->threads->data[i]);
    }

    vector_destroy(current_process->threads);
    current_process->threads = vector_create(sizeof(struct thread*));

    vfs_get_pathname(node, current_process->name, sizeof(current_process->name) - 1);

    struct thread* new_thread = thread_create(current_process, entry, NULL, argv, envp, true);
    if (new_thread == NULL) {
        vmm_destroy_pagemap(new_pagemap);
        r->rax = (uint64_t) -1;
        return;
    }

    vmm_switch_pagemap(kernel_pagemap);
    vmm_destroy_pagemap(old_pagemap);

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

    struct thread* new_thread = thread_create(this_cpu()->running_thread->process, entry, NULL, NULL, NULL, true);
    sched_thread_enqueue(new_thread);

    r->rax = (uint64_t) new_thread->tid;
}

void syscall_thread_exit(struct registers* r) {
    (void) r;
    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}

void syscall_sbrk(struct registers* r) {
    intptr_t size = r->rdi;

    void* ptr = process_sbrk(this_cpu()->running_thread->process, size);
    if (ptr == NULL) {
        r->rax = (uint64_t) -1;
    } else {
        r->rax = (uint64_t) ptr;
    }
}
