#include <cpu/isr.h>
#include <dev/cmos.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/user_access.h>

void syscall_sleep(struct registers* r) {
    const struct timespec* duration = (const struct timespec*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_sleep (duration: 0x%x) on (pid: %u, tid: %u)\n",
            (uintptr_t) duration, current_process->pid, current_thread->tid);

    struct timespec duration_copy;
    if (copy_from_user(&duration_copy, duration, sizeof(struct timespec)) == NULL) {
        r->rax = -EFAULT;
        return;
    }

    if (duration_copy.tv_nsec < 0 || duration_copy.tv_nsec > 999999999 || duration_copy.tv_sec < 0) {
        r->rax = -EINVAL;
        return;
    }

    uint64_t duration_ns = duration_copy.tv_nsec;
    duration_ns += duration_copy.tv_sec * 1000000000;

    sched_thread_sleep(current_thread, duration_ns);
    r->rax = 0;
}

void syscall_clock_gettime(struct registers* r) {
    clockid_t clk_id = r->rdi;
    struct timespec* ts = (struct timespec*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getclock (clk_id: %d, ts: 0x%x) on (pid: %u, tid: %u)\n",
            clk_id, (uintptr_t) ts, current_process->pid, current_thread->tid);

    int ret = 0;

    struct timespec* source = NULL;

    switch (clk_id) {
        case CLOCK_REALTIME:
            source = &time_realtime;
            break;
        case CLOCK_MONOTONIC:
            source = &time_monotonic;
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
            source = &current_process->ticks;
            break;
        case CLOCK_THREAD_CPUTIME_ID:
            source = &current_thread->ticks;
            break;
        default:
            ret = -EINVAL;
            break;
    }

    if (ret == 0) {
        if (copy_to_user((void*) ts, (void*) source, sizeof(struct timespec)) == NULL) {
            ret = -EFAULT;
        }
    }

    r->rax = ret;
}

void syscall_clock_settime(struct registers* r) {
    clockid_t clk_id = r->rdi;
    const struct timespec* ts = (struct timespec*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_setclock (clk_id: %d, ts: 0x%x) on (pid: %u, tid: %u)\n",
            clk_id, (uintptr_t) ts, current_process->pid, current_thread->tid);

    int ret = 0;

    switch (clk_id) {
        case CLOCK_REALTIME:
            if (copy_from_user((void*) &time_realtime, (void*) ts, sizeof(struct timespec)) == NULL) {
                ret = -EFAULT;
            }
            break;
        case CLOCK_MONOTONIC:
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
            ret = -EPERM;
            break;
        default:
            ret = -EINVAL;
            break;
    }

    r->rax = ret;
}
