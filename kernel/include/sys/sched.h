#ifndef _KERNEL_SYS_SCHED_H
#define _KERNEL_SYS_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>

#define SCHED_VECTOR 48

extern struct process* kernel_process;

static inline void sched_resched_now(void) {
    __asm__ volatile("cli; int $%P0" :: "p" (SCHED_VECTOR));
}

__attribute__((noreturn)) void sched_await(void);
bool sched_enqueue_thread(struct thread* t);
bool sched_dequeue_thread(struct thread* t);
void sched_thread_sleep(struct thread* t, uint64_t ns);
__attribute__((noreturn)) void sched_yield(void);
void sched_init(void);

#endif /* _KERNEL_SYS_SCHED_H */
