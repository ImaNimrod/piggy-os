#include <cpu/asm.h>
#include <stddef.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    if (lock == NULL) {
        return;
    }

    volatile size_t deadlock_counter = 0;
    for (;;) {
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            break;
        }

        if (deadlock_counter++ >= 100000000) {
            kpanic(NULL, true, "deadlock at 0x%016lx", __builtin_return_address(0));
        }

        pause();
    }
}

bool spinlock_test_and_acquire(spinlock_t* lock) {
    if (lock == NULL) {
        return false;
    }

    return __sync_bool_compare_and_swap(lock, 0, 1);
}

void spinlock_release(spinlock_t* lock) {
    if (lock == NULL) {
        return;
    }

    __atomic_store_n(lock, 0, __ATOMIC_SEQ_CST);
}
