#ifndef _KERNEL_UTILS_MUTEX_H
#define _KERNEL_UTILS_MUTEX_H

#include <stdatomic.h>
#include <utils/wait_queue.h>

typedef struct mutex {
    _Atomic(struct thread*) owner;
    struct wait_queue wq;
} mutex_t; 

void mutex_init(mutex_t* m);
void mutex_acquire(mutex_t* m);
void mutex_release(mutex_t* m);

#endif /* _KERNEL_UTILS_MUTEX_H */
