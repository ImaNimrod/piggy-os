#ifndef _KERNEL_SYS_SCHED_H
#define _KERNEL_SYS_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>

#define SCHED_VECTOR 48

extern struct process* kernel_process;

__attribute__((noreturn)) void sched_await(void);
bool sched_thread_enqueue(struct thread* t);
bool sched_thread_dequeue(struct thread* t);
void sched_thread_destroy(struct thread* t);
void sched_thread_sleep(struct thread* t, uint64_t ms);
__attribute__((noreturn)) void sched_yield(void);
void sched_init(void);

#endif /* _KERNEL_SYS_SCHED_H */
