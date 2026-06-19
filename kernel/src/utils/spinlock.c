#include <cpu/asm.h>
#include <stddef.h>
#include <utils/log.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    volatile size_t deadlock_counter = 0;

    for (;;) {
        if (spinlock_test_and_acquire(lock)) {
            return;
        }

        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            if (++deadlock_counter > 100000000) {
                kpanic(NULL, true, "deadlock");
            }
            pause();
        }
    }
}

bool spinlock_acquire_irqsave(spinlock_t* lock) {
    bool int_state = get_interrupt_state();

    cli();
    spinlock_acquire(lock);

    return int_state;
}

void spinlock_release(spinlock_t* lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

void spinlock_release_irqsave(spinlock_t* lock, bool int_state) {
    spinlock_release(lock);

    if (int_state) {
        sti();
    }
}

bool spinlock_test_and_acquire(spinlock_t* lock) {
    spinlock_t expected = 0;
    return __atomic_compare_exchange_n(lock, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
