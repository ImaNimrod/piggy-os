#ifndef _KERNEL_SYS_TIMER_H
#define _KERNEL_SYS_TIMER_H

#include <types.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

extern struct timespec time_monotonic;
extern struct timespec time_realtime;

struct thread;

void timer_sleep_thread(struct thread* thread, const struct timespec* tp);
void timer_update_timers(void);
void timer_init(void);

#endif /* _KERNEL_SYS_TIMER_H */
