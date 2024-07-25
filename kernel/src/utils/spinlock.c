#include <cpu/asm.h>
#include <utils/log.h>
#include <utils/spinlock.h>

void spinlock_acquire(spinlock_t* lock) {
    if (!lock) {
        return;
    }

    //size_t count = 0;

    for (;;) {
        if (spinlock_test_and_acquire(lock)) {
            break;
        }

        /*
        count++;
        if (count >= 10000000) {
            kpanic(NULL, "deadlock!");
        }
        */

        pause();
    }
}

void spinlock_release(spinlock_t* lock) {
    if (!lock) {
        return;
    }
    __sync_bool_compare_and_swap(lock, 1, 0);
}
