#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <sys/sched.h>
#include <types.h>

// TODO: implement fork
void syscall_fork(struct registers* r) {
    r->rax = (uint64_t) -1;
}

void syscall_exit(struct registers* r) {
    r->rax = (uint64_t) -1;
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

// TODO: implement thread create
void syscall_thread_create(struct registers* r) {
    r->rax = (uint64_t) -1;
}

void syscall_thread_exit(struct registers* r) {
    (void) r;
    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}
