#include <sys/syscall.h>
#include "time_internal.h"

int clock_settime(clockid_t clk_id, const struct timespec* tp) {
    return syscall2(SYS_CLOCK_SETTIME, clk_id, (uint64_t) tp);
}

