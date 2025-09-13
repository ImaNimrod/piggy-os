#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>

void syscall_getppid(struct registers* r) {
    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (unlikely(current_process->parent == NULL)) {
        r->rax = -1;
    } else {
        r->rax = current_process->parent->pid;
    }
}
