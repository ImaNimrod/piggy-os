#ifndef _KERNEL_UTILS_SPINLOCK_H
#define _KERNEL_UTILS_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t spinlock_t;

static inline bool spinlock_test_and_acquire(spinlock_t* lock) {
    return __sync_bool_compare_and_swap(lock, 0, 1);
}

void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);

#endif /* _KERNEL_UTILS_SPINLOCK_H */
