#include "time_internal.h"

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) < 0) {
        return -1;
    }

    clock_t res;
    if (__builtin_mul_overflow(ts.tv_sec, CLOCKS_PER_SEC, &res) || __builtin_add_overflow(res, ts.tv_nsec / 1000, &res)) {
        return -1;
    }
    return res;
}
