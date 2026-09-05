#ifndef _KERNEL_SYS_FUTEX_H
#define _KERNEL_SYS_FUTEX_H

#include <stdint.h>
#include <sys/timer.h>
#include <utils/wait_queue.h>

struct futex {
    struct wait_queue wq;
    int waiters;
};

int futex_wait(uintptr_t futex_paddr, uint32_t* addr, uint32_t value, const struct timespec* timeout);
int futex_wake(uintptr_t futex_paddr);
void futex_init(void);

#endif /* _KERNEL_SYS_FUTEX_H */
