#ifndef _KERNEL_DEV_CMOS_H
#define _KERNEL_DEV_CMOS_H

#include <types.h>

#define HWCLOCK_GETTIME 0x1001
#define HWCLOCK_SETTIME 0x1002

#define RTC_DEV_MAJOR 5

struct rtc_time {
    int second;
    int minute;
    int hour;
    int day;
    int month;
    int year;
};

time_t cmos_get_rtc_timestamp(void);
void cmos_init(void);

#endif /* _KERNEL_DEV_CMOS_H */
