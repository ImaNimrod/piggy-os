#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/scheduler.h>
#include <utils/usercopy.h>

void sys_sleep(struct registers* r) {
    const struct timespec* duration = (const struct timespec*) r->rdi;

    struct timespec kduration;

    int ret = user_memcpy_from_user(&kduration, duration, sizeof(struct timespec));
    if (ret < 0) {
        r->rax = ret;
        return;
    }

    if (!timespec_validate(&kduration)) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = scheduler_sleep(this_cpu()->scheduler.current_thread, &kduration);
}
