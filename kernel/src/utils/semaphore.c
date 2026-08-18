#include <utils/semaphore.h>

void semaphore_init(semaphore_t* s, unsigned int value) {
    atomic_init(&s->value, value);
    wait_queue_init(&s->wq);
}

void semaphore_reset(semaphore_t* s) {
    atomic_store_explicit(&s->value, 0, memory_order_release);
}

void semaphore_signal(semaphore_t* s) {
    atomic_fetch_add_explicit(&s->value, 1, memory_order_release);
    wait_queue_wake_one(&s->wq);
}

void semaphore_wait(semaphore_t* s) {
    for (;;) {
        unsigned int value = atomic_load_explicit(&s->value, memory_order_acquire);

        while (value > 0) {
            if (atomic_compare_exchange_weak_explicit(&s->value, &value, value - 1, memory_order_acquire, memory_order_relaxed)) {
                return;
            }
        }

        wait_queue_wait(&s->wq, false);
    }
}
