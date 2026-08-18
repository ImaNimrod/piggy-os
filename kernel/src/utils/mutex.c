#include <cpu/smp.h>
#include <utils/log.h>
#include <utils/mutex.h>

#define OPTIMISTIC_SPIN_COUNT 100

void mutex_init(mutex_t* m) {
    m->owner = NULL;
    wait_queue_init(&m->wq);
}

void mutex_acquire(mutex_t* m) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    for (;;) {
        struct thread* expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&m->owner, &expected, current_thread, memory_order_acquire, memory_order_relaxed)) {
            return;
        }

        for (size_t i = 0; i < OPTIMISTIC_SPIN_COUNT; i++) {
            expected = NULL;
            if (atomic_compare_exchange_weak_explicit(&m->owner, &expected, current_thread, memory_order_acquire, memory_order_relaxed)) {
                return;
            }
        }

        wait_queue_wait(&m->wq, false);
    }
}

void mutex_release(mutex_t* m) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct thread* expected = current_thread;

    if (!atomic_compare_exchange_strong_explicit(&m->owner, &expected, NULL, memory_order_release, memory_order_relaxed)) {
        if (unlikely(!expected)) {
            kpanic(NULL, true, "mutex was double unlocked");
        }

        kpanic(NULL, true, "mutex unlocked by thread that does not own it");
    }

    wait_queue_wake_one(&m->wq);
}
