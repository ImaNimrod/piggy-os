#ifndef _KERNEL_SYS_SCHEDULER_H
#define _KERNEL_SYS_SCHEDULER_H

#include <sys/process.h>
#include <utils/spinlock.h>

#define SCHEDULER_IRQ_VECTOR 48
#define SCHEDULER_TIME_QUANTA_MS 10

struct scheduler {
    struct thread* current_thread; // do not move
    struct thread* idle_thread;

    struct thread* run_queue_head;
    struct thread* run_queue_tail;
    spinlock_t run_queue_lock;
};

[[noreturn]] void scheduler_await(void);
void scheduler_dequeue(struct scheduler* sched, struct thread* thread);
void scheduler_enqueue(struct scheduler* sched, struct thread* thread);
void scheduler_prepare_wait(struct thread* thread, bool interruptable);
void scheduler_sleep(struct thread* thread, const struct timespec* duration);
[[noreturn]] void scheduler_thread_exit(void);
bool scheduler_wakeup(struct thread* thread, thread_wakeup_reason_t wakeup_reason);
thread_wakeup_reason_t scheduler_yield(void);

void scheduler_init(void);
void scheduler_percpu_init(void);

#endif /* _KERNEL_SYS_SCHEDULER_H */ 
