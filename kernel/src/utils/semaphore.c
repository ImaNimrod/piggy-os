#include <cpu/smp.h>
#include <sys/scheduler.h>
#include <utils/semaphore.h>

void semaphore_init(semaphore_t* s, uint64_t value) {
    s->value = value;
    s->waiters = NULL;
    spinlock_init(&s->lock);
}

void semaphore_reset(semaphore_t* s) {
    bool int_state = spinlock_acquire_irqsave(&s->lock);
    s->value = 0;
    spinlock_release_irqsave(&s->lock, int_state);
}

void semaphore_signal(semaphore_t* s) {
    bool int_state = spinlock_acquire_irqsave(&s->lock);

    struct thread* waiter = NULL;

    if (s->waiters != NULL) {
        waiter = s->waiters;
        s->waiters = waiter->next_waiter;
        waiter->next_waiter = NULL;
    } else {
        s->value++;
    }

    spinlock_release_irqsave(&s->lock, int_state);

    if (waiter != NULL) {
        scheduler_unblock(waiter);
    }
}

void semaphore_wait(semaphore_t* s) {
    bool int_state = spinlock_acquire_irqsave(&s->lock);

    if (s->value > 0) {
        s->value--;
        spinlock_release_irqsave(&s->lock, int_state);
        return;
    }

    struct thread* current = this_cpu()->scheduler.current_thread;
    current->next_waiter = NULL;

    struct thread* iter = s->waiters;
    if (iter == NULL) {
        s->waiters = current;
    } else {
        while (iter->next_waiter != NULL) {
            iter = iter->next_waiter;
        }
        iter->next_waiter = current;
    }

    scheduler_block_and_release(this_cpu()->scheduler.current_thread, &s->lock, int_state);
}
