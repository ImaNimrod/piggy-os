#ifndef _KERNEL_UTILS_SPINLOCK_H
#define _KERNEL_UTILS_SPINLOCK_H

#include <stdatomic.h>

typedef atomic_bool spinlock_t;

#define spinlock_init(lock) \
    do { \
        atomic_init(lock, false); \
    } while (0);

void spinlock_acquire(spinlock_t* lock);
bool spinlock_acquire_irqsave(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);
void spinlock_release_irqsave(spinlock_t* lock, bool int_state);
bool spinlock_test_and_acquire(spinlock_t* lock);

#endif /* _KERNEL_UTILS_SPINLOCK_H */
