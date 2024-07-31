#include <cpu/asm.h>
#include <dev/acpi/acpi.h>
#include <dev/cmos.h>
#include <utils/log.h>

#define CMOS_ADDRESS_PORT   0x70
#define CMOS_DATA_PORT      0x71

static bool bcd_mode = false;
static bool xxiv_hr_mode = false;
static uint8_t century_register = 0;

static inline int bcd_to_bin(uint8_t val) {
    return (val & 0x0f) + ((val & 0xf0) >> 4) * 10;
}

static inline bool is_cmos_updating(void) {
    outb(CMOS_ADDRESS_PORT, 0x0a);
    return (inb(CMOS_DATA_PORT) & 0x80);
}


static inline bool is_leap_year(uint32_t year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static uint8_t month_to_days(uint8_t month, uint32_t year) {
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
        default: kpanic(NULL, true, "invalid month value recieved: %u\n", month);
    }
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, reg);
    return bcd_mode ? inb(CMOS_DATA_PORT) : bcd_to_bin(inb(CMOS_DATA_PORT));
}

void cmos_get_rtc_time(struct timespec* tp) {
    while (is_cmos_updating()) {
        pause();
    }

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint32_t year = cmos_read(0x09);

    if (!xxiv_hr_mode && (hour & 0x80)) {
        hour = ((hour & 0x7f) + 12) % 24;
    }

    if (century_register != 0) {
        uint8_t century = cmos_read(century_register);
        year += century * 100; 
    } else {
        // no timetravellers allowed!
        year += (year < 20) ? 21 * 100 : 20 * 100;
    }

    time_t days_since_epoch = day - 1;
    for (uint8_t i = 1; i < month; i++) {
        days_since_epoch += month_to_days(i, year);
    }
    for (uint32_t i = 1970; i < year; i++) {
        days_since_epoch += is_leap_year(i) ? 366 : 365;
    } 

    tp->tv_sec =  second + (minute * 60) + (hour * 3600) + (days_since_epoch * 86400);
    tp->tv_nsec = 0;
}

void cmos_init(void) {
    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (fadt != NULL) {
        uint8_t acpi_century_register = *(uint8_t*) ((uintptr_t) fadt + 108);
        if (acpi_century_register != 0) {
            century_register = acpi_century_register;
        }
    }

    while (is_cmos_updating()) {
        pause();
    }

    uint8_t status = cmos_read(0x0b);
    if (status & (1 << 1)) {
        xxiv_hr_mode = true;
    }
    if (status & (1 << 2)) {
        bcd_mode = true;
    }
}
