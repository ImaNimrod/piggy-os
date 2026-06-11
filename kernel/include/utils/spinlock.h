#ifndef _KERNEL_UTILS_SPINLOCK_H
#define _KERNEL_UTILS_SPINLOCK_H

#include <stdint.h>

typedef uint32_t spinlock_t;

#define spinlock_init(lock) \
    do { \
        *lock = (spinlock_t) {0}; \
    } while (0);

void spinlock_acquire(spinlock_t* lock);
bool spinlock_acquire_irqsave(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);
void spinlock_release_irqsave(spinlock_t* lock, bool int_state);
bool spinlock_test_and_acquire(spinlock_t* lock);

#endif /* _KERNEL_UTILS_SPINLOCK_H */
