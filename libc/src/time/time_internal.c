#include "time_internal.h"

time_t __days_per_month(int month, time_t year) {
    switch (month) {
        case 0: return 31;
        case 1: return __is_leap_year(year) ? 29 : 28;
        case 2: return 31;
        case 3: return 30;
        case 4: return 31;
        case 5: return 30;
        case 6: return 31;
        case 7: return 31;
        case 8: return 30;
        case 9: return 31;
        case 10: return 30;
        case 11: return 31;
    }
    __builtin_unreachable();
}

int __get_first_weekday_of_year(const struct tm* tm) {
    int weekday = (tm->tm_wday - tm->tm_yday) % 7;
    return weekday < 0 ? weekday + 7 : weekday;
}

time_t __get_week_based_year(const struct tm* tm) {
    int first_weekday = __get_first_weekday_of_year(tm);

    time_t year = (time_t) tm->tm_year + 1900;
    if (first_weekday == 0 || first_weekday >= 5) {
        year--;
    }
    return year;
}
