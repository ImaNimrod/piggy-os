#include "time_internal.h"

struct tm* gmtime_r(const time_t* time, struct tm* tm) {
    time_t seconds_left = *time;
    time_t year = 1970;
    time_t days_since_epoch = 0;

    while (seconds_left < 0) {
        year--;
        seconds_left += __days_per_year(year) * 86400;
        days_since_epoch -= __days_per_year(year);
    }

    while (seconds_left >= __days_per_year(year) * 86400) {
        seconds_left -= __days_per_year(year) * 86400;
        days_since_epoch += __days_per_year(year);
        year++;
    }

    tm->tm_year = year - 1900;

    tm->tm_mon = 0;
    tm->tm_yday = 0;
    while (seconds_left >= __days_per_month(tm->tm_mon, year) * 86400) {
        seconds_left -= __days_per_month(tm->tm_mon, year) * 86400;
        tm->tm_yday += __days_per_month(tm->tm_mon, year);
        days_since_epoch += __days_per_month(tm->tm_mon, year);
        tm->tm_mon++;
    }

    int day = seconds_left / 86400;
    tm->tm_mday = day + 1;
    tm->tm_yday += day;
    days_since_epoch += day;
    seconds_left %= 86400;

    tm->tm_hour = seconds_left / (60 * 60);
    seconds_left %= (60 * 60);

    tm->tm_min = seconds_left / 60;
    tm->tm_sec = seconds_left % 60;
    tm->tm_isdst = 0;

    tm->tm_wday = (4 + days_since_epoch) % 7;
    if (tm->tm_wday < 0) {
        tm->tm_wday += 7;
    }

    return tm;
}
