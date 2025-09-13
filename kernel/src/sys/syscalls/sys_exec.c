#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/paging.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/macros.h>

void syscall_exec(struct registers* r) {
    const char* path = (const char*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int error = 0;

    struct pagemap* old_pagemap = current_process->pagemap;
    struct pagemap* new_pagemap = pagemap_create();
    if (unlikely(new_pagemap == NULL)) {
        error = -ENOMEM;
        goto error;
    }

    struct vfs_node* node;
    if ((error = vfs_lookup(vfs_root, path, false, NULL, &node)) < 0) {
        goto error;
    }

    if (node->type != VFS_TYPE_REGULAR) {
        error = -ENOEXEC;
        goto error;
    }

    uintptr_t entry;
    if ((error = elf_load(new_pagemap, node, &entry)) < 0) {
        goto error;
    }

    node->ops->unlock(node);

    cli(); // no going back after this point

    current_process->pagemap = new_pagemap;
    current_process->thread_stack_top = PROCESS_STACK_TOP;

    struct thread* t;
    for (size_t i = 0; i < vector_size(current_process->threads); i++) {
        t = *vector_get(current_process->threads, i);
        if (t != this_cpu()->running_thread) {
            scheduler_dequeue(t);
            thread_destroy(t);
        }
    }
    vector_destroy(current_process->threads);

    current_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(current_process->threads == NULL)) {
        error = -ENOMEM;
        goto error;
    }

    struct thread* new_thread = thread_create_user(current_process, entry);
    if (unlikely(new_thread == NULL)) {
        error = -ENOMEM;
        goto error;
    }
    scheduler_enqueue(new_thread);

    pagemap_load(kernel_pagemap);
    pagemap_destroy(old_pagemap);

    r->rax = 0;

    scheduler_dequeue(this_cpu()->running_thread);
    thread_destroy(this_cpu()->running_thread);
    this_cpu()->running_thread = NULL;
    scheduler_yield(false);
    __builtin_unreachable();

error:
    if (current_process->pagemap == old_pagemap) {
        if (new_pagemap != NULL) {
            pagemap_destroy(new_pagemap);
        }
    } else {
        process_exit(current_process, -1);
    }

    r->rax = error;
}
