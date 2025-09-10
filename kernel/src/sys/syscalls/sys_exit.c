#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>
#include <sys/scheduler.h>

void syscall_exit(struct registers* r) {
    int status = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    process_exit(current_process, status);

    this_cpu()->running_thread = NULL;
    scheduler_yield(false);
    __builtin_unreachable();
}
