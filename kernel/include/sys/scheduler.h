#ifndef _KERNEL_SYS_SCHEDULER_H
#define _KERNEL_SYS_SCHEDULER_H 1

#include <sys/process.h>
#include <types.h>
#include <utils/macros.h>

NORETURN void scheduler_await(void);
void scheduler_yield(void);
void scheduler_thread_enqueue(struct thread* t);
void scheduler_thread_dequeue(struct thread* t);
void scheduler_thread_block(struct thread* t);
void scheduler_thread_sleep(struct thread* t, const struct timespec* tp);
void scheduler_thread_unblock(struct thread* t);
void scheduler_init(void);

#endif /* _KERNEL_SYS_SCHEDULER_H */ 
