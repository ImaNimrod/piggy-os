#ifndef _KERNEL_DEV_CMOS_H
#define _KERNEL_DEV_CMOS_H

#define RTC_DEV_MAJOR 5

#define RTC_GET_TIME 0x1001
#define RTC_SET_TIME 0x1002

#include <types.h>

time_t cmos_get_rtc_timestamp(void);
void cmos_init(void);

void rtc_dev_init(void);

#endif /* _KERNEL_DEV_CMOS_H */
