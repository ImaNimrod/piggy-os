#include <errno.h>
#include "time_internal.h"

static bool normalize(int* x, int* y, int range) {
    if (__builtin_add_overflow(*y, *x / range, y)) {
        return false;
    }

    *x %= range;
    if (*x < 0) {
        *x += range;
        (*y)--;
    }

    return true;
}

static bool normalize_entries(struct tm* tm) {
    if (!normalize(&tm->tm_sec, &tm->tm_min, 60) ||
            !normalize(&tm->tm_min, &tm->tm_hour, 60) ||
            !normalize(&tm->tm_hour, &tm->tm_mday, 24)) {
        return false;
    }

    if (!normalize(&tm->tm_mon, &tm->tm_year, 12)) {
        return false;
    }

    while (tm->tm_mday > __days_per_month(tm->tm_mon,
                (time_t) tm->tm_year + 1900)) {
        tm->tm_mday -= __days_per_month(tm->tm_mon, (time_t) tm->tm_year + 1900);
        tm->tm_mon++;
        if (tm->tm_mon > 11) {
            if (__builtin_add_overflow(tm->tm_year, 1, &tm->tm_year)) {
                return false;
            }
            tm->tm_mon = 0;
        }
    }

    while (tm->tm_mday <= 0) {
        tm->tm_mon--;
        if (tm->tm_mon < 0) {
            if (__builtin_sub_overflow(tm->tm_year, 1, &tm->tm_year)) {
                return false;
            }
            tm->tm_mon = 11;
        }
        tm->tm_mday += __days_per_month(tm->tm_mon, (time_t) tm->tm_year + 1900);
    }

    return true;
}

time_t timegm(struct tm* tm) {
    if (!normalize_entries(tm)) {
        errno = EOVERFLOW;
        return -1;
    }

    time_t year = 1970;
    time_t days_since_epoch = 0;

    while (year < 1900 + (time_t) tm->tm_year) {
        days_since_epoch += __days_per_year(year);
        year++;
    }

    while (year > 1900 + (time_t) tm->tm_year) {
        year--;
        days_since_epoch -= __days_per_year(year);
    }

    int month = 0;
    tm->tm_yday = 0;

    while (month < tm->tm_mon) {
        days_since_epoch += __days_per_month(month, year);
        tm->tm_yday += __days_per_month(month, year);
        month++;
    }

    days_since_epoch += tm->tm_mday - 1;

    time_t seconds_since_epoch = days_since_epoch * 24 * 60 * 60;
    tm->tm_yday += tm->tm_mday - 1;

    seconds_since_epoch += tm->tm_hour * 60 * 60;
    seconds_since_epoch += tm->tm_min * 60;
    seconds_since_epoch += tm->tm_sec;

    tm->tm_wday = (4 + days_since_epoch) % 7;
    if (tm->tm_wday < 0) {
        tm->tm_wday += 7;
    }

    return seconds_since_epoch;
}
