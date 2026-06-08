#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>

void sys_getpgid(struct registers* r) {
    pid_t pid = r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (pid == 0 || pid == current_process->pid) {
        r->rax = current_process->group->pgid;
    } else {
        struct process* target = process_find_by_pid(pid);
        if (target != NULL) {
            r->rax = target->group->pgid;
        } else {
            r->rax = -ESRCH;
        }
    }
}
