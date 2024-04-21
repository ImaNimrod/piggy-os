#include <cpu/asm.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    if (!lock) {
        return;
    }

    while (!spinlock_test_and_acquire(lock)) {
        pause();
    }
}

void spinlock_release(spinlock_t* lock) {
    if (!lock) {
        return;
    }

    __sync_bool_compare_and_swap(lock, 1, 0);
}
