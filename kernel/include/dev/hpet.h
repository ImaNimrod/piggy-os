#ifndef _KERNEL_DEV_HPET_H
#define _KERNEL_DEV_HPET_H

#include <stdint.h>
#include <sys/timer.h>

extern struct timer_driver hpet_driver;

uint64_t hpet_calibrate_tsc(void);

#endif /* _KERNEL_DEV_HPET_H */
