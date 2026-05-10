#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/scheduler.h>
#include <utils/usercopy.h>

void sys_sleep(struct registers* r) {
    const struct timespec* duration = (const struct timespec*) r->rdi;

    struct timespec kduration;
    int ret;
    if ((ret = user_memcpy_from_user(&kduration, duration, sizeof(struct timespec))) < 0) {
        r->rax = ret;
        return;
    }

    if (kduration.tv_nsec < 0 || kduration.tv_nsec > 999999999 || kduration.tv_sec < 0) {
        r->rax = -EINVAL;
        return;
    }

    scheduler_sleep(this_cpu()->scheduler.current_thread, &kduration);
    r->rax = 0;
}
