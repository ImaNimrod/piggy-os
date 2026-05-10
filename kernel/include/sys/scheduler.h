#ifndef _KERNEL_SYS_SCHEDULER_H
#define _KERNEL_SYS_SCHEDULER_H

#include <stdbool.h>
#include <sys/process.h>
#include <sys/timer.h>
#include <utils/macros.h>
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

void scheduler_block(struct thread* thread);
void scheduler_block_and_release(struct thread* thread, spinlock_t* lock, bool int_state);
void scheduler_dequeue(struct scheduler* sched, struct thread* thread);
void scheduler_enqueue(struct scheduler* sched, struct thread* thread);
void scheduler_sleep(struct thread* thread, const struct timespec* duration);
NORETURN void scheduler_thread_exit(void);
void scheduler_unblock(struct thread* thread);
void scheduler_yield(bool save);

void scheduler_init(void);

#endif /* _KERNEL_SYS_SCHEDULER_H */ 
