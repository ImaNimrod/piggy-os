#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/usercopy.h>

void sys_sigpending(struct registers* r) {
    sigset_t* set = (sigset_t*) r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    spinlock_acquire(&current_thread->signal_lock);

    r->rax = user_memcpy_to_user(set, &current_thread->pending_signals, sizeof(sigset_t));

    spinlock_release(&current_thread->signal_lock);
}
