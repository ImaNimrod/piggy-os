#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
        pause();
    }
}

void spinlock_release(spinlock_t* lock) {
    __sync_bool_compare_and_swap(lock, 1, 0);
}

