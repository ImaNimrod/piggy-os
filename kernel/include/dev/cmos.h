#ifndef _KERNEL_DEV_CMOS_H
#define _KERNEL_DEV_CMOS_H 1

#include <types.h>

void cmos_get_rtc_time(struct timespec* tp);
void cmos_init(void);

#endif /* _KERNEL_DEV_CMOS_H */
