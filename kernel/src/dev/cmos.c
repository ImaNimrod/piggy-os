#include <cpu/asm.h>
#include <dev/cmos.h>
#include <errno.h>
#include <fs/devfs.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/timer.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#define CMOS_ADDRESS_PORT   0x70
#define CMOS_DATA_PORT      0x71

#define CMOS_REG_SECOND     0x00
#define CMOS_REG_MINUTE     0x02
#define CMOS_REG_HOUR       0x04
#define CMOS_REG_MDAY       0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_STATUS_A   0x0a
#define CMOS_REG_STATUS_B   0x0b

static int rtc_ioctl(dev_t dev, int request, void* argp);

static uint8_t century_register;
static bool bcd_mode;
static bool xxiv_hr_mode;

static struct device_ops rtc_ops = {
    .ioctl = rtc_ioctl,
};

static inline uint8_t bcd_to_bin(uint8_t value) {
    return (value & 0x0f) + ((value & 0xf0) >> 4) * 10;
}

static inline uint8_t bin_to_bcd(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, reg);
    return bcd_mode ? bcd_to_bin(inb(CMOS_DATA_PORT)) : inb(CMOS_DATA_PORT);
}

static inline void cmos_write(uint8_t reg, uint8_t data) {
    outb(CMOS_ADDRESS_PORT, reg);

    if (bcd_mode) {
        data = bin_to_bcd(data);
    }
    outb(CMOS_DATA_PORT, data);
}

static inline bool is_cmos_updating(void) {
    outb(CMOS_ADDRESS_PORT, CMOS_REG_STATUS_A);
    return (inb(CMOS_DATA_PORT) & 0x80);
}

static inline bool is_leap_year(int year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static void get_rtc_time(struct rtc_time* result) {
    while (is_cmos_updating()) {
        pause();
    }

    int second = cmos_read(CMOS_REG_SECOND);
    int minute = cmos_read(CMOS_REG_MINUTE);
    int hour = cmos_read(CMOS_REG_HOUR);
    int day = cmos_read(CMOS_REG_MDAY);
    int month = cmos_read(CMOS_REG_MONTH);
    int year = cmos_read(CMOS_REG_YEAR);

    int century = 0;
    if (century_register != 0) {
        century = cmos_read(century_register);
    }

    if (!xxiv_hr_mode && (hour & 0x80)) {
        hour = ((hour & 0x7f) + 12) % 24;
    }

    if (century_register != 0) {
        year += century * 100; 
    } else {
        // try to prevent timetravellers
        year += (year < 20) ? 21 * 100 : 20 * 100;
    }

    result->second = second;
    result->minute = minute;
    result->hour = hour;
    result->day = day;
    result->month = month;
    result->year = year;
}

static int month_to_days(int month, int year) {
    switch (month) {
        case 1: return 31;
        case 2: return is_leap_year(year) ? 29 : 28;
        case 3: return 31;
        case 4: return 30;
        case 5: return 31;
        case 6: return 30;
        case 7: return 31;
        case 8: return 31;
        case 9: return 30;
        case 10: return 31;
        case 11: return 30;
        case 12: return 31;
        default: return 0;
    }
}

static int set_rtc_time(const struct rtc_time* time) {
    if (time->second < 0 || time->second > 59) {
        return -EINVAL;
    }
    if (time->minute < 0 || time->minute > 59) {
        return -EINVAL;
    }
    if (time->hour < 0 || time->hour > 23) {
        return -EINVAL;
    }
    if (time->month < 1 || time->month > 12) {
        return -EINVAL;
    }
    if (time->day < 1 || time->day > month_to_days(time->month, time->year)) {
        return -EINVAL;
    }

    uint8_t century = time->year / 100;
    uint8_t year = time->year % 100;

    if (century < 19 || century > 99) {
        return -EINVAL;
    }

    uint8_t hour = time->hour;
    if (!xxiv_hr_mode) {
        bool pm = hour >= 12;

        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }

        if (pm) {
            hour |= (1 << 7);
        }
    }

    while (is_cmos_updating()) {
        pause();
    }

    cmos_write(CMOS_REG_SECOND, time->second);
    cmos_write(CMOS_REG_MINUTE, time->minute);
    cmos_write(CMOS_REG_HOUR, hour);
    cmos_write(CMOS_REG_MDAY, time->day);
    cmos_write(CMOS_REG_MONTH, time->month);
    cmos_write(CMOS_REG_YEAR, year);

    if (century_register != 0) {
        cmos_write(century_register, century);
    }

    return 0;
}

static int rtc_ioctl(dev_t dev, int request, void* argp) {
    (void) dev;

    struct rtc_time time;

    int ret = 0;

    switch (request) {
        case HWCLOCK_GETTIME:
            get_rtc_time(&time);

            ret = user_memcpy_to_user(argp, &time, sizeof(struct rtc_time));
            break;
        case HWCLOCK_SETTIME:
            ret = user_memcpy_from_user(&time, argp, sizeof(struct rtc_time));
            if (ret < 0) {
                break;
            }

            set_rtc_time(&time);
            break;
        default:
            ret = -ENOTTY;
            break;
    }

    return ret;
}

time_t cmos_get_rtc_timestamp(void) {
    struct rtc_time time;
    get_rtc_time(&time);

    time_t days_since_epoch = time.day - 1;
    for (int i = 1; i < time.month; i++) {
        days_since_epoch += month_to_days(i, time.year);
    }

    for (int i = 1970; i < time.year; i++) {
        days_since_epoch += is_leap_year(i) ? 366 : 365;
    } 

    return time.second + (time.minute * 60) + (time.hour * 3600) + (days_since_epoch * 86400);
}

void cmos_init(void) {
    struct uacpi_table table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_FADT_SIGNATURE, &table);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "unable to find FADT table: %s", uacpi_status_to_string(ret));
    }

    struct acpi_fadt* fadt_table = table.ptr;
    if (fadt_table->iapc_boot_arch & ACPI_IA_PC_NO_CMOS_RTC) {
        klog("[cmos] system lacks a legacy CMOS RTC device\n");
        uacpi_table_unref(&table);
        return;
    }

    century_register = fadt_table->century;

    uacpi_table_unref(&table);

    while (is_cmos_updating()) {
        pause();
    }

    uint8_t status = cmos_read(CMOS_REG_STATUS_B);
    if (status & (1 << 1)) {
        xxiv_hr_mode = true;
    }
    if (!(status & (1 << 2))) {
        bcd_mode = true;
    }

    time_t timestamp = cmos_get_rtc_timestamp();

    time_realtime.tv_sec = timestamp;
    struct timespec ts = timer_time_from_boot();
    timespec_add(&time_realtime, &ts);

    klog("[cmos] initialized RTC (timestamp: %lu)\n", timestamp);
}

void cmos_init_rtc_dev(void) {
    if (unlikely(devfs_register("rtc", VFS_TYPE_CHARDEV, &rtc_ops, makedev(RTC_DEV_MAJOR, 0)) < 0)) {
        kpanic(NULL, false, "failed to create RTC device");
    }
}
