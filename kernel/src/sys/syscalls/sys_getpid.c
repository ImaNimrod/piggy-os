#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>

void sys_getpid(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    r->rax = current_process->pid;
}
