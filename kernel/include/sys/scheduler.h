#ifndef _KERNEL_SYS_SCHEDULER_H
#define _KERNEL_SYS_SCHEDULER_H

#include <stdbool.h>
#include <sys/process.h>
#include <sys/timer.h>
#include <utils/macros.h>

#define SCHEDULER_IRQ_VECTOR 48
#define SCHEDULER_TIME_QUANTA_MS 10

NORETURN void scheduler_await(void);
void scheduler_block(struct thread* t);
void scheduler_dequeue(struct thread* t);
void scheduler_enqueue(struct thread* t);
void scheduler_sleep(struct thread* t, const struct timespec* tp);
void scheduler_unblock(struct thread* t);
void scheduler_yield(bool save);
void scheduler_init(void);

#endif /* _KERNEL_SYS_SCHEDULER_H */ 
