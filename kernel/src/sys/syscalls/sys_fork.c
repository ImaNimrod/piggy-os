#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/scheduler.h>

void sys_fork(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct process* new_process = process_create(current_process);
    if (new_process == NULL) {
        r->rax = -ENOMEM;
        return;
    }

    struct thread* new_thread = thread_fork(new_process, r);
    if (new_thread == NULL) {
        process_destroy(new_process);
        r->rax = -ENOMEM;
        return;
    }

    r->rax = new_process->pid;

    scheduler_enqueue(new_thread);
}
