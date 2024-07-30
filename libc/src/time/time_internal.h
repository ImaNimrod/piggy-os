#ifndef TIME_INTERNAL_H
#define TIME_INTERNAL_H

#include <stdbool.h>
#include <time.h>

static inline bool __is_leap_year(time_t year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static inline time_t __days_per_year(time_t year) {
    return __is_leap_year(year) ? 366 : 365;
}

static inline time_t __time_abs(time_t t) {
    return t < 0 ? -t : t;
}

time_t __days_per_month(int, time_t);
int __get_first_weekday_of_year(const struct tm*);
time_t __get_week_based_year(const struct tm*);

#endif /* TIME_INTERNAL_H */
