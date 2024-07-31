#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

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

struct tm* gmtime(const time_t*);
struct tm* gmtime_r(const time_t* __restrict, struct tm* __restrict);
struct tm* localtime(const time_t*); 
time_t mktime(struct tm*);
size_t strftime(char* __restrict, size_t, const char* __restrict, const struct tm* __restrict);
time_t time(time_t*);
time_t timegm(struct tm*);

#endif /* _TIME_H */
