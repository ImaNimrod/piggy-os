#include <cpu/isr.h>
#include <errno.h>
#include <fs/file.h>
#include <utils/usercopy.h>

// TODO: actually implement pipes

#define VALID_FLAGS (O_NONBLOCK | O_CLOEXEC)

void sys_pipe(struct registers* r) {
    int* fds = (int*) r->rdi;
    int flags = r->rsi;

    if (!IS_USER_ADDRESS(fds)) {
        r->rax = -EFAULT;
        return;
    }

    if (flags & ~VALID_FLAGS) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = -ENOSYS;
}
