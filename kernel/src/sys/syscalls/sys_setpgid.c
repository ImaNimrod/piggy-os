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
        process = process_find(pid);
        if (!process) {
            r->rax = -ESRCH;
            return;
        }
    }

    int ret = 0;

    pid_t target_pgid = pgid ? pgid : process->pid;

    struct process_group* new_group = process_group_find(target_pgid);
    if (new_group) {
        process_group_move(new_group, process);
        process_group_unref(new_group);
        goto end;
    }

    if (target_pgid != process->pid) {
        ret = -ESRCH;
        goto end;
    }

    new_group = process_group_create(target_pgid);
    if (!new_group) {
        ret = -ENOMEM;
        goto end;
    }

    process_group_move(new_group, process);

end:
    if (process != current_process) {
        PROCESS_UNREF(process);
    }

    r->rax = ret;
}
