#include <sys/syscall.h>
#include "time_internal.h"

int clock_gettime(clockid_t clk_id, struct timespec* tp) {
    return syscall2(SYS_CLOCK_GETTIME, clk_id, (uint64_t) tp);
}

