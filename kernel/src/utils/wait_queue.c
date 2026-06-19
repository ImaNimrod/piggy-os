#include <cpu/smp.h>
#include <errno.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/wait_queue.h>

void wait_queue_init(struct wait_queue* wq) {
    wq->head = wq->tail = NULL;
    spinlock_init(&wq->lock);
}

int wait_queue_wait(struct wait_queue* wq) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    struct wait_node* node = kmalloc(sizeof(struct wait_node));
    if (unlikely(node == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for struct wait_node");
    }
    node->thread = current_thread;
    node->next = NULL;

    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    if (wq->tail != NULL) {
        wq->tail->next = node;
    } else {
        wq->head = node;
    }

    wq->tail = node;

    scheduler_prepare_wait(current_thread, false);

    spinlock_release_irqsave(&wq->lock, int_state);

    return (scheduler_yield() == THREAD_WAKEUP_REASON_INTERRUPTED) ? EINTR : 0;
}

void wait_queue_wake_all(struct wait_queue* wq) {
    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    struct wait_node* node = wq->head;
    wq->head = wq->tail = NULL;

    spinlock_release_irqsave(&wq->lock, int_state);

    while (node != NULL) {
        struct wait_node* next = node->next;
        scheduler_wakeup(node->thread, THREAD_WAKEUP_REASON_NORMAL);
        node = next;
    }
}

void wait_queue_wake_one(struct wait_queue* wq) {
    bool int_state = spinlock_acquire_irqsave(&wq->lock);

    struct wait_node* node = wq->head;
    if (node == NULL) {
        spinlock_release_irqsave(&wq->lock, int_state);
        return;
    }

    wq->head = node->next;
    if (wq->head == NULL) {
        wq->tail = NULL;
    }

    spinlock_release_irqsave(&wq->lock, int_state);
    scheduler_wakeup(node->thread, THREAD_WAKEUP_REASON_NORMAL);
}
