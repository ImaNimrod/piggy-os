#include <cpu/asm.h>
#include <stddef.h>
#include <utils/log.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    volatile size_t deadlock_counter = 0;

    for (;;) {
        while (atomic_load_explicit(lock, memory_order_relaxed)) {
            if (++deadlock_counter > 100000000) {
                kpanic(NULL, true, "deadlock");
            }

            pause();
        }

        if (spinlock_test_and_acquire(lock)) {
            return;
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
    atomic_store_explicit(lock, false, memory_order_release);
}

void spinlock_release_irqsave(spinlock_t* lock, bool int_state) {
    spinlock_release(lock);

    if (int_state) {
        sti();
    }
}

bool spinlock_test_and_acquire(spinlock_t* lock) {
    return !atomic_exchange_explicit(lock, true, memory_order_acquire);
}
