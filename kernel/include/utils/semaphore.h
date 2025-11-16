#ifndef _KERNEL_UTILS_SEMAPHORE_H
#define _KERNEL_UTILS_SEMAPHORE_H

#include <stdint.h>
#include <sys/process.h>
#include <utils/spinlock.h>

typedef struct semaphore {
    spinlock_t lock;
    uint64_t value;
    struct thread* waiters;
} semaphore_t;

void semaphore_init(semaphore_t* s, uint64_t value);
void semaphore_signal(semaphore_t* s);
void semaphore_wait(semaphore_t* s);

#endif /* _KERNEL_UTILS_SEMAPHORE_H */
