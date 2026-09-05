#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <sys/futex.h>
#include <sys/process.h>
#include <utils/usercopy.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

void sys_futex(struct registers* r) {
    uint32_t* addr = (uint32_t*) r->rdi;
    int op = r->rsi;
    uint32_t value = r->rdx;
    const struct timespec* timeout = (const struct timespec*) r->r10;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(addr)) {
        r->rax = -EFAULT;
        return;
    }

    struct timespec ktimeout;
    if (timeout) {
        int ret = user_memcpy_from_user(&ktimeout, timeout, sizeof(struct timespec));
        if (ret < 0) {
            r->rax = ret;
            return;
        }

        if (!timespec_validate(&ktimeout)) {
            r->rax = -EINVAL;
            return;
        }
    }

    page_size_t unused;

    uintptr_t paddr = pagemap_get_mapping(current_process->vmm_context->pagemap, (uintptr_t) addr, &unused);
    if (paddr == 0) {
        r->rax = -EINVAL;
        return;
    }

    switch (op) {
        case FUTEX_WAIT:
            r->rax = futex_wait(paddr, addr, value, timeout ? &ktimeout : NULL);
            break;
        case FUTEX_WAKE:
            r->rax = futex_wake(paddr);
            break;
        default:
            r->rax = -ENOSYS;
            break;
    }
}
