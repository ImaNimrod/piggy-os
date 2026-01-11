#include <cpu/smp.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/mutex.h>

void mutex_init(mutex_t* m) {
    spinlock_init(&m->lock);
    m->owner = NULL;
    m->waiters = NULL;
}

void mutex_acquire(mutex_t* m) {
    bool int_state = spinlock_acquire_irqsave(&m->lock);

    struct thread* current_thread = this_cpu()->running_thread;

    if (m->owner == NULL) {
        m->owner = current_thread;
        spinlock_release_irqsave(&m->lock, int_state);
        return;
    }

    current_thread->next_waiter = m->waiters;
    m->waiters = current_thread;

    scheduler_block_and_release(current_thread, &m->lock, int_state);
    m->owner = current_thread;
}

void mutex_release(mutex_t* m) {
    bool int_state = spinlock_acquire_irqsave(&m->lock);

    struct thread* current_thread = this_cpu()->running_thread;

    if (m->owner != current_thread) {
        kpanic(NULL, true, "mutex unlocked by thread that does not own it");
    }

    m->owner = NULL;

    if (m->waiters != NULL) {
        struct thread* waiter = m->waiters;
        m->waiters = waiter->next_waiter;
        scheduler_unblock(waiter);
    }

    spinlock_release_irqsave(&m->lock, int_state);
}
