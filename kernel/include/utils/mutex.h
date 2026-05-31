#ifndef _KERNEL_UTILS_MUTEX_H
#define _KERNEL_UTILS_MUTEX_H

#include <utils/spinlock.h>

struct thread;

typedef struct {
    spinlock_t lock;
    struct thread* owner;

    struct thread* waiters_head;
    struct thread* waiters_tail;
} mutex_t; 

void mutex_init(mutex_t* m);
void mutex_acquire(mutex_t* m);
void mutex_release(mutex_t* m);

#endif /* _KERNEL_UTILS_MUTEX_H */
