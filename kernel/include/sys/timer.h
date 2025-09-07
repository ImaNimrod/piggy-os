#ifndef _KERNEL_SYS_TIMER_H
#define _KERNEL_SYS_TIMER_H

#include <sys/scheduler.h>
#include <types.h>

extern struct timespec time_monotonic;
extern struct timespec time_realtime;

void timer_sleep_thread(struct thread* thread, const struct timespec* tp);
void timer_update_timers(void);
void timer_init(void);

#endif /* _KERNEL_SYS_TIMER_H */
