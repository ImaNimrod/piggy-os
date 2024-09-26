#include <cpu/asm.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    if (!lock) {
        return;
    }

    volatile size_t counter = 0;

    for (;;) {
        if (spinlock_test_and_acquire(lock)) {
            break;
        }

        if (++counter >= 100000000) {
            kpanic(NULL, true, "deadlock!");
        }

        pause();
    }
}

void spinlock_release(spinlock_t* lock) {
    if (!lock) {
        return;
    }
    __atomic_store_n(lock, 0, __ATOMIC_SEQ_CST);
}
