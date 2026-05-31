#include <cpu/smp.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/mutex.h>

void mutex_init(mutex_t* m) {
    spinlock_init(&m->lock);
    m->owner = NULL;

    m->waiters_head = NULL;
    m->waiters_tail = NULL;
}

void mutex_acquire(mutex_t* m) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    for (;;) {
        bool int_state = spinlock_acquire_irqsave(&m->lock);

        if (m->owner == NULL) {
            m->owner = current_thread;
            spinlock_release_irqsave(&m->lock, int_state);
            return;
        }

        current_thread->next_waiter = NULL;

        if (m->waiters_tail == NULL) {
            m->waiters_head = current_thread;
            m->waiters_tail = current_thread;
        } else {
            m->waiters_tail->next_waiter = current_thread;
            m->waiters_tail = current_thread;
        }

        scheduler_block_and_release(current_thread, &m->lock, int_state);
    }
}

void mutex_release(mutex_t* m) {
    bool int_state = spinlock_acquire_irqsave(&m->lock);

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    if (m->owner != current_thread) {
        kpanic(NULL, true, "mutex unlocked by thread that does not own it");
    }

    m->owner = NULL;

    struct thread* waiter = NULL;

    if (m->waiters_head != NULL) {
        waiter = m->waiters_head;
        m->waiters_head = waiter->next_waiter;

        if (m->waiters_head == NULL) {
            m->waiters_tail = NULL;
        }

        waiter->next_waiter = NULL;
    }

    spinlock_release_irqsave(&m->lock, int_state);

    if (waiter != NULL) {
        scheduler_unblock(waiter);
    }
}
