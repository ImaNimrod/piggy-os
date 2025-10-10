#include <cpu/isr.h>
#include <errno.h> 
#include <sys/timer.h> 
#include <types.h>
#include <utils/usercopy.h>

void sys_gettime(struct registers* r) {
    clockid_t clockid = r->rdi;
    struct timespec* tp = (struct timespec*) r->rsi;

    struct timespec* source;

    switch (clockid) {
        case CLOCK_REALTIME:
            source = &time_realtime;
            break;
        case CLOCK_MONOTONIC:
            source = &time_monotonic;
            break;
        default:
            r->rax = -EINVAL;
            return;
    }

    r->rax = user_memcpy_to_user((void*) tp, (void*) source, sizeof(struct timespec));
}
