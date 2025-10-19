#include <cpu/isr.h> 
#include <cpu/smp.h> 
#include <errno.h>
#include <fs/file.h>
#include <sys/process.h>

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_DUPFD_CLOEXEC 1000

#define FD_CLOEXEC 1

void sys_fcntl(struct registers* r) {
    int fd = r->rdi;
    int op = r->rsi;
    int arg = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    int ret;

    switch (op) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            ret = file_dup(current_process, fd, arg, false, op & F_DUPFD_CLOEXEC);
            break;
        case F_GETFD:
            ret = current_process->fds[fd].cloexec ? FD_CLOEXEC : 0;
            break;
        case F_SETFD:
            current_process->fds[fd].cloexec = (arg & FD_CLOEXEC);
            ret = 0;
            break;
        case F_GETFL:
            ret = file->flags;
            break;
        case F_SETFL:
            file->flags &= ~(O_APPEND | O_NONBLOCK);
            file->flags |= (arg & (O_APPEND | O_NONBLOCK));
            ret = 0;
            break;
        default:
            ret = -EINVAL;
            break;
    }

    file_release(file);

    r->rax = ret;
}
