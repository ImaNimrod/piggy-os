#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/signal.h>
#include <utils/usercopy.h>

void sys_sigsuspend(struct registers* r) {
    const sigset_t* mask = (const sigset_t*) r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    spinlock_acquire(&current_thread->signal_lock);

    current_thread->return_signal_mask = current_thread->signal_mask;

    int ret = 0;
    if ((ret = user_memcpy_from_user(&current_thread->signal_mask, mask, sizeof(sigset_t))) < 0) {
        spinlock_release(&current_thread->signal_lock);
        r->rax = ret;
        return;
    }

    spinlock_acquire(&current_thread->state_lock);
    current_thread->flags |= THREAD_FLAG_RETURN_SIGNAL_MASK;
    spinlock_release(&current_thread->state_lock);

    spinlock_release(&current_thread->signal_lock);

    scheduler_prepare_wait(current_thread, true);
    scheduler_yield();

    r->rax = EINTR;
}
