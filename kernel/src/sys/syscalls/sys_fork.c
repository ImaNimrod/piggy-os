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
    if (new_process == NULL) {
        r->rax = -ENOMEM;
        return;
    }

    struct thread* new_thread = thread_fork(new_process, current_thread, r);
    if (new_thread == NULL) {
        vmm_context_destroy(new_process->vmm_context);
        process_destroy(new_process);
        r->rax = -ENOMEM;
        return;
    }

    r->rax = new_process->pid;
}
