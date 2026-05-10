#include <cpu/smp.h>
#include <sys/scheduler.h>
#include <utils/event.h>

void event_init(struct event* event) {
    event->pending = 0;
    event->waiters = NULL;
    spinlock_init(&event->lock);
}

ssize_t event_wait(struct event* event, bool block) {
    bool int_state = spinlock_acquire_irqsave(&event->lock);

    if (event->pending > 0) {
        event->pending--;
        spinlock_release_irqsave(&event->lock, int_state);
        return 0;
    }

    if (!block) {
        spinlock_release_irqsave(&event->lock, int_state);
        return -1;
    }

    this_cpu()->scheduler.current_thread->next_waiter = event->waiters;
    event->waiters = this_cpu()->scheduler.current_thread;

    scheduler_block_and_release(this_cpu()->scheduler.current_thread, &event->lock, int_state);
    return 0;
}

size_t event_trigger(struct event* event) {
    bool int_state = spinlock_acquire_irqsave(&event->lock);

    if (event->waiters == NULL) {
        event->pending++;
        spinlock_release_irqsave(&event->lock, int_state);
        return 0;
    }

    size_t woken = 0;

    struct thread* waiter = event->waiters;
    event->waiters = NULL;

    while (waiter != NULL) {
        struct thread* next = waiter->next_waiter;
        waiter->next_waiter = NULL;

        scheduler_unblock(waiter);
        woken++;

        waiter = next;
    }

    spinlock_release_irqsave(&event->lock, int_state);
    return woken;
}
