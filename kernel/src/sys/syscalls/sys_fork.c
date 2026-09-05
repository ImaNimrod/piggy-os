#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/scheduler.h>

void sys_fork(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    struct process* new_process = process_create(current_process);
    if (!new_process) {
        r->rax = -ENOMEM;
        return;
    }

    struct thread* new_thread = thread_fork(new_process, current_thread, r);
    if (!new_thread) {
        vmm_context_destroy(new_process->vmm_context);
        PROCESS_UNREF(new_process);

        r->rax = -ENOMEM;
        return;
    }

    scheduler_enqueue(&new_thread->cpu->scheduler, new_thread);

    PROCESS_UNREF(new_process);

    r->rax = new_process->pid;
}
