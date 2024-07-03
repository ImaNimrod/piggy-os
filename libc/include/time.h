#ifndef _TIME_H
#define _TIME_H

#include <sys/types.h>

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

// TODO: implement time.h functions
size_t strftime(char* __restrict, size_t, const char* __restrict, const struct tm* __restrict);
time_t time(time_t*);

#endif /* _TIME_H */
