#ifndef _KERNEL_UTILS_WAIT_QUEUE_H
#define _KERNEL_UTILS_WAIT_QUEUE_H

#include <utils/spinlock.h>

struct wait_node {
    struct thread* thread;
    struct wait_node* prev;
    struct wait_node* next;
};

struct wait_queue {
    struct wait_node* head;
    struct wait_node* tail;
    spinlock_t lock;
};

void wait_queue_init(struct wait_queue* wq);
void wait_queue_add(struct wait_queue* wq, struct wait_node* node);
int wait_queue_wait(struct wait_queue* wq);
void wait_queue_wake_all(struct wait_queue* wq);
void wait_queue_wake_one(struct wait_queue* wq);

#endif /* _KERNEL_UTILS_WAIT_QUEUE_H */
