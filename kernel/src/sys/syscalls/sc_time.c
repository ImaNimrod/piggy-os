#include <cpu/isr.h>
#include <dev/cmos.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/user_access.h>

void syscall_getclock(struct registers* r) {
    clockid_t clktyp = r->rdi;
    struct timespec* tp = (struct timespec*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getclock (clktyp: %d, tp: 0x%x) on (pid: %u, tid: %u)\n",
            clktyp, (uintptr_t) tp, current_process->pid, current_thread->tid);

    int ret = 0;

    switch (clktyp) {
        case CLOCK_REALTIME:
            if (copy_to_user((void*) tp, (void*) &time_realtime, sizeof(struct timespec)) == NULL) {
                ret = -EFAULT;
            }
            break;
        case CLOCK_MONOTONIC:
            if (copy_to_user((void*) tp, (void*) &time_monotonic, sizeof(struct timespec)) == NULL) {
                ret = -EFAULT;
            }
            break;
        default:
            ret = -EINVAL;
            break;
    }

    r->rax = ret;
}
