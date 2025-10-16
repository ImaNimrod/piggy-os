#include <cpu/isr.h>
#include <errno.h> 
#include <sys/timer.h> 
#include <utils/usercopy.h>

void sys_settime(struct registers* r) {
    clockid_t clockid = r->rdi;
    const struct timespec* tp = (const struct timespec*) r->rsi;

    int ret = 0;

    switch (clockid) {
        case CLOCK_REALTIME:
            ret = user_memcpy_from_user(&time_realtime, tp, sizeof(struct timespec));
            break;
        case CLOCK_MONOTONIC:
            ret = -ENOTSUP;
            break;
        default:
            ret = -EINVAL;
            break;
    }

    r->rax = ret;
}
