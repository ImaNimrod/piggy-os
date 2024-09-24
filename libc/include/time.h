#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

#define CLOCKS_PER_SEC 1000000

#define CLOCK_REALTIME              0
#define CLOCK_MONOTONIC             1
#define CLOCK_PROCESS_CPUTIME_ID    2
#define CLOCK_THREAD_CPUTIME_ID     3

typedef long int clock_t;

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

clock_t clock(void);
int clock_gettime(clockid_t, struct timespec*);
int clock_settime(clockid_t, const struct timespec*);
double difftime(time_t, time_t);
struct tm* gmtime(const time_t*);
struct tm* gmtime_r(const time_t* __restrict, struct tm* __restrict);
struct tm* localtime(const time_t*); 
time_t mktime(struct tm*);
size_t strftime(char* __restrict, size_t, const char* __restrict, const struct tm* __restrict);
time_t time(time_t*);
time_t timegm(struct tm*);

#endif /* _TIME_H */
