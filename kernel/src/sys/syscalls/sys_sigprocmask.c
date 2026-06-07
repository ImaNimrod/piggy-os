#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/usercopy.h>

#define SIG_BLOCK   1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

void sys_sigprocmask(struct registers* r) {
    int how = r->rdi;
    const sigset_t* set = (const sigset_t*) r->rsi;
    sigset_t* oldset = (sigset_t*) r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    spinlock_acquire(&current_thread->signal_lock);

    int ret = 0;

    if (oldset != NULL) {
        if ((ret = user_memcpy_to_user(oldset, &current_thread->signal_mask, sizeof(sigset_t))) < 0) {
            goto end;
        }
    }

    if (set != NULL) {
        sigset_t kset;

        if ((ret = user_memcpy_from_user(&kset, set, sizeof(sigset_t))) < 0) {
            goto end;
        }

        kset &= ~UNBLOCKABLE_SIGNALS;

        switch (how) {
            case SIG_BLOCK:
                current_thread->signal_mask |= kset;
                break;
            case SIG_UNBLOCK:
                current_thread->signal_mask &= ~kset;
                break;
            case SIG_SETMASK:
                current_thread->signal_mask = kset;
                break;
            default:
                ret = -EINVAL;
                break;
        }
    }

end:
    spinlock_release(&current_thread->signal_lock);
    r->rax = ret;
}
