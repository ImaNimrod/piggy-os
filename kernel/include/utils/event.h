#ifndef _KERNEL_UTILS_EVENT_H
#define _KERNEL_UTILS_EVENT_H

#include <sys/process.h>
#include <utils/spinlock.h> 

struct event {
    size_t pending;
    struct thread* waiters;
    spinlock_t lock;
};

void event_init(struct event* event);
ssize_t event_wait(struct event* event, bool block);
size_t event_trigger(struct event* event);

#endif /* _KERNEL_UTILS_EVENT_H */
