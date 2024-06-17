#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <mem/vmm.h>
#include <sys/sched.h>
#include <types.h>
#include <utils/log.h>

// TODO: implement fork
void syscall_fork(struct registers* r) {
    r->rax = (uint64_t) -1;
}

void syscall_exit(struct registers* r) {
    (void) r;

    struct process* p = this_cpu()->running_thread->process;

    // TODO: use exit code argument in some way

    if (p->pid < 2) {
        kpanic(NULL, "tried to exit init process");
    }

    vmm_switch_pagemap(&kernel_pagemap);
    this_cpu()->running_thread->process = kernel_process;

    cli();

    for (size_t i = 0; i < p->threads->size; i++) {
        sched_thread_destroy(p->threads->data[i]);
    }

	if (p->parent) {
		vector_remove_by_value(p->parent->children, p);
	}

    // TODO: reparent process children to init process
    /*
    struct process* init = process_list->next;

    for (size_t i = 0; i < p->children->size; i++) {
        struct process* child = p->children->data[i];
        child->parent = init;
        vector_push(init->children, child);
    }
    */

    process_destroy(p);
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
