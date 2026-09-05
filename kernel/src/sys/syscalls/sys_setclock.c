#include <cpu/isr.h>
#include <errno.h> 
#include <sys/timer.h> 
#include <utils/usercopy.h>

void sys_setclock(struct registers* r) {
    clockid_t clockid = r->rdi;
    const struct timespec* tp = (const struct timespec*) r->rsi;

    if (clockid == CLOCK_REALTIME) {
        struct timespec ktp;

        int ret = user_memcpy_from_user(&ktp, tp, sizeof(struct timespec));
        if (ret < 0) {
            r->rax = ret;
            return;
        }

        if (!timespec_validate(&ktp)) {
            r->rax = -EINVAL;
            return;
        }

        time_realtime = ktp;
        r->rax = 0;
    } else {
        r->rax =  -EINVAL;
    }
}
