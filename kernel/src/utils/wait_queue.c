#include <cpu/smp.h>
#include <errno.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/wait_queue.h>

static void internal_add(struct wait_queue* wq, struct wait_node* node) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    node->thread = current_thread;

    node->next = NULL;
    node->prev = wq->tail;

    if (wq->tail) {
        wq->tail->next = node;
    } else {
        wq->head = node;
    }

    wq->tail = node;
}

void wait_queue_init(struct wait_queue* wq) {
    wq->head = wq->tail = NULL;
    spinlock_init(&wq->lock);
}

void wait_queue_add(struct wait_queue* wq, struct wait_node* node) {
    bool int_state = spinlock_acquire_irqsave(&wq->lock);
    internal_add(wq, node);
    spinlock_release_irqsave(&wq->lock, int_state);
}

int wait_queue_wait(struct wait_queue* wq, bool interruptable) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    internal_add(wq, &current_thread->wait_node);

    scheduler_prepare_wait(current_thread, interruptable);
    spinlock_release_irqsave(&wq->lock, int_state);

    return scheduler_yield();
}

void wait_queue_wake_all(struct wait_queue* wq) {
    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    struct wait_node* node = wq->head;
    wq->head = wq->tail = NULL;

    while (node) {
        struct wait_node* next = node->next;

        node->prev = node->next = NULL;
        scheduler_wakeup(node->thread, 0);

        node = next;
    }

    spinlock_release_irqsave(&wq->lock, int_state);
}

void wait_queue_wake_one(struct wait_queue* wq) {
    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    struct wait_node* node = wq->head;
    if (!node) {
        spinlock_release_irqsave(&wq->lock, int_state);
        return;
    }

    wq->head = node->next;
    if (!wq->head) {
        wq->tail = NULL;
    }

    node->prev = node->next = NULL;

    spinlock_release_irqsave(&wq->lock, int_state);
    scheduler_wakeup(node->thread, 0);
}
