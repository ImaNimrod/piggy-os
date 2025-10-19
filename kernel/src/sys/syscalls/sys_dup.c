#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <sys/process.h>

#define VALID_FLAGS (O_CLOEXEC)

void sys_dup(struct registers* r) {
    int old_fd = r->rdi;
    int new_fd = r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (old_fd == new_fd || flags & ~VALID_FLAGS) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = file_dup(current_process, old_fd, new_fd, true, flags & O_CLOEXEC);
}
