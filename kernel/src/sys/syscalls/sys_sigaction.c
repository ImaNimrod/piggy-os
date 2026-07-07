#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/usercopy.h>

// TODO: actually implement SA_SIGINFO, its stubbed now for pthread
#define VALID_FLAGS (SA_NODEFER | SA_ONSTACK | SA_RESTART | SA_SIGINFO)

void sys_sigaction(struct registers* r) {
    int signal = r->rdi;
    const struct sigaction* act = (const struct sigaction*) r->rsi;
    struct sigaction* oldact = (struct sigaction*) r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (signal <= 0 || signal >= NSIG || signal == SIGKILL || signal == SIGSTOP) {
        r->rax = -EINVAL;
        return;
    }

    spinlock_acquire(&current_process->signal_actions_lock);

    int ret = 0;

    if (oldact) {
        if ((ret = user_memcpy_to_user(oldact, &current_process->signal_actions[signal - 1], sizeof(struct sigaction))) < 0) {
            goto end;
        }
    }

    if (act) {
        struct sigaction newact;

        if ((ret = user_memcpy_from_user(&newact, act, sizeof(struct sigaction))) < 0) {
            goto end;
        }

        if (newact.sa_flags & ~VALID_FLAGS) {
            ret = -EINVAL;
            goto end;
        }

        current_process->signal_actions[signal - 1] = newact;
    }

end:
    spinlock_release(&current_process->signal_actions_lock);
    r->rax = ret;
}
