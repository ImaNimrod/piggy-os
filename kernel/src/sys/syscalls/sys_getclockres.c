#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/timer.h> 
#include <utils/usercopy.h>

void sys_getclockres(struct registers* r) {
    clockid_t clockid = r->rdi;
    struct timespec* tp = (struct timespec*) r->rsi;

    struct timespec resolution;

    switch (clockid) {
        case CLOCK_REALTIME:
        case CLOCK_MONOTONIC:
        case CLOCK_BOOTTIME:
            uint64_t period_ns = 1000000000ULL / this_cpu()->timer_info->hz;

            resolution.tv_sec = period_ns / 1000000000ULL;
            resolution.tv_nsec = period_ns % 1000000000ULL;
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
            resolution.tv_sec = 0;
            resolution.tv_nsec = SCHEDULER_TIME_QUANTA_MS * 1000000ULL;
            break;
        default:
            r->rax = -EINVAL;
            return;
    }

    r->rax = user_memcpy_to_user(tp, &resolution, sizeof(resolution));
}
