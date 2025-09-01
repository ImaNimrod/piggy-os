#include <cpu/asm.h>
#include <stddef.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            pause();
        }
    }
}

bool spinlock_test_and_acquire(spinlock_t* lock) {
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) == 0;
}

void spinlock_release(spinlock_t* lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}
