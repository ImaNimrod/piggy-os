#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/list.h>
#include <utils/usercopy.h>

#include <utils/log.h>
static void signal_all_processes(struct process* process, int signal) {
    if (!process) {
        return;
    }

    if (process != init_process) {
        klog("pid: %d\n", process->pid);
        signal_send_process(process, signal);
    }

    struct process* child;
    SLIST_FOREACH(process->children, child, sibling_next) {
        signal_all_processes(child, signal);
    }
}

void sys_kill(struct registers* r) {
    pid_t pid = r->rdi;
    int signal = r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (signal <= 0 || signal >= NSIG) {
        r->rax = -EINVAL;
        return;
    }

    if (pid > 0) {
        struct process* target = process_find(pid);
        if (!target) {
            r->rax = -ESRCH;
            return;
        }

        if (signal == 0) {
            r->rax = 0;
            return;
        }

        r->rax = signal_send_process(target, signal);
    } else if (pid == 0) {
        r->rax = signal_send_process_group(current_process->group, signal);
    }  else if (pid == -1) {
        if (signal == 0) {
            r->rax = 0;
            return;
        }

        if (!init_process->children) {
            r->rax = -ESRCH;
            return;
        }

        signal_all_processes(init_process, signal);
        r->rax = 0;
    } else {
        struct process_group* target = process_group_find(-pid);
        if (!target) {
            r->rax = -ESRCH;
            return;
        }

        if (signal == 0) {
            r->rax = 0;
        } else {
            r->rax = signal_send_process_group(target, signal);
        }

        process_group_unref(target);
    }
}
