#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>

void syscall_getppid(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    r->rax = current_process->parent->pid;
}
