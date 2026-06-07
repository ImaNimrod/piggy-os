#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/usercopy.h>

#define VALID_FLAGS (SS_DISABLE)

#include <utils/log.h>

void sys_sigaltstack(struct registers* r) {
    const stack_t* ss = (const stack_t*) r->rdi;
    stack_t* oldss = (stack_t*) r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    int ret = 0;

    if (oldss != NULL) {
        spinlock_acquire(&current_thread->signal_lock);
        stack_t current_stack = current_thread->signal_stack;
        spinlock_release(&current_thread->signal_lock);

        if (signal_on_altstack(current_thread, r->rsp)) {
            current_stack.ss_flags |= SS_ONSTACK;
        }

        if ((ret = user_memcpy_to_user(oldss, &current_stack, sizeof(stack_t))) < 0) {
            goto end;
        }
    }

    if (ss == NULL) {
        goto end;
    }

    stack_t newss;

    if ((ret = user_memcpy_from_user(&newss, ss, sizeof(stack_t))) < 0) {
        goto end;
    }

    if (newss.ss_flags & ~VALID_FLAGS) {
        ret = -EINVAL;
        goto end;
    }

    if (!(newss.ss_flags & SS_DISABLE)) {
        if (!IS_USER_ADDRESS(newss.ss_sp)) {
            ret = -EINVAL;
            goto end;
        }

        if (newss.ss_size < MINSIGSTKSZ) {
            ret = -ENOMEM;
            goto end;
        }
    }

    spinlock_acquire(&current_thread->signal_lock);

    if (current_thread->signal_stack.ss_flags & SS_ONSTACK) {
        spinlock_release(&current_thread->signal_lock);
        ret = -EPERM;
        goto end;
    }

    current_thread->signal_stack = newss;
    spinlock_release(&current_thread->signal_lock);

end:
    r->rax = ret;
}
