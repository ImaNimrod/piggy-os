#include <cpu/isr.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/list.h>
#include <utils/usercopy.h>

static void signal_all_processes(struct process* process, int signal) {
    if (process == NULL) {
        return;
    }

    signal_send_process(process, signal);

    struct process* child = process->children;

    while (child != NULL) {
        signal_all_processes(child, signal);
        child = child->next;
    }
}

void sys_kill(struct registers* r) {
    pid_t pid = r->rdi;
    int signal = r->rsi;

    if (signal <= 0 || signal >= NSIG) {
        r->rax = -EINVAL;
        return;
    }

    if (pid == -1) {
        if (signal == 0) {
            r->rax = 0;
            return;
        }

        if (init_process->children == NULL) {
            r->rax = -ESRCH;
            return;
        }

        signal_all_processes(init_process->children, signal);
        r->rax = 0;
    } else if (pid > 0) {
        struct process* target = process_find_by_pid(pid);
        if (target == NULL) {
            r->rax = -ESRCH;
            return;
        }

        if (signal == 0) {
            r->rax = 0;
            return;
        }

        signal_send_process(target, signal);
        r->rax = 0;
    } else {
        r->rax = -ENOTSUP;
    }
}
