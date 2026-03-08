#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <sys/process.h>
#include <sys/timer.h> 
#include <utils/usercopy.h>

void sys_getclock(struct registers* r) {
    clockid_t clockid = r->rdi;
    struct timespec* tp = (struct timespec*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct timespec source;

    switch (clockid) {
        case CLOCK_REALTIME:
            source = time_realtime;
            break;
        case CLOCK_MONOTONIC:
        case CLOCK_BOOTTIME:
            source = timer_time_from_boot();
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
            source = current_process->time_used;
            break;
        case CLOCK_THREAD_CPUTIME_ID:
            source = current_thread->time_used;
            break;
        default:
            r->rax = -EINVAL;
            return;
    }

    r->rax = user_memcpy_to_user(tp, &source, sizeof(struct timespec));
}
