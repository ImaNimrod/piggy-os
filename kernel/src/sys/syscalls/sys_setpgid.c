#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>

void sys_setpgid(struct registers* r) {
    pid_t pid = r->rdi;
    pid_t pgid = r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (pgid < 0) {
        r->rax = -EINVAL;
        return;
    }

    struct process* process;

    if (pid == 0 || pid == current_process->pid) {
        process = current_process;
    } else {
        process = process_find_by_pid(pid);
    }

    if (!process) {
        r->rax = -ESRCH;
        return;
    }

    struct process_group* new_process_group = process_group_find_by_pgid((pgid == 0) ? process->pid : pgid);
    if (!new_process_group) {
        process_group_remove(process->group, process);

        if (unlikely(!process_group_create(process))) {
            r->rax = -ENOMEM;
            return;
        }
    } else {
        process_group_move(new_process_group, process);
    }

    r->rax = 0;
}
