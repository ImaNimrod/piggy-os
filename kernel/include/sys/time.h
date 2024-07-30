#ifndef _KERNEL_SYS_TIME_H
#define _KERNEL_SYS_TIME_H

#include <types.h>

extern struct timespec time_monotonic;
extern struct timespec time_realtime;

void time_update_timers(void);
void time_init(void);

#endif /* _KERNEL_SYS_TIME_H */
