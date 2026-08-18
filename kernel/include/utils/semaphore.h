#ifndef _KERNEL_UTILS_SEMAPHORE_H
#define _KERNEL_UTILS_SEMAPHORE_H

#include <stdatomic.h>
#include <utils/wait_queue.h>

typedef struct {
    atomic_uint value;
    struct wait_queue wq;
} semaphore_t;

void semaphore_init(semaphore_t* s, unsigned int value);
void semaphore_reset(semaphore_t* s);
void semaphore_signal(semaphore_t* s);
void semaphore_wait(semaphore_t* s);

#endif /* _KERNEL_UTILS_SEMAPHORE_H */
