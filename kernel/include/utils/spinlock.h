#ifndef _KERNEL_UTILS_SPINLOCK_H
#define _KERNEL_UTILS_SPINLOCK_H

#include <cpu/asm.h>
#include <stdint.h>

typedef uint64_t spinlock_t;

void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);

#endif /* _KERNEL_UTILS_SPINLOCK_H */
