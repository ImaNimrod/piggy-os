#include <sys/syscall.h>
#include "time_internal.h"

static inline int getclock(int clktyp, struct timespec* tp) {
    return syscall2(SYS_GETCLOCK, clktyp, (uint64_t) tp);
}

time_t time(time_t* result) {
    struct timespec ts;
    if (getclock(CLOCK_REALTIME, &ts) < 0) {
        return -1;
    }

    if (result) {
        *result = ts.tv_sec;
    }
    return ts.tv_sec;
}
