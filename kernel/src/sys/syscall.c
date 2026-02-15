#include <cpu/isr.h>
#include <errno.h>
#include <syscall.h>
#include <utils/macros.h>

extern void sys_exit(struct registers* r);
extern void sys_fork(struct registers* r);
extern void sys_exec(struct registers* r);
extern void sys_wait(struct registers* r);
extern void sys_getpid(struct registers* r);
extern void sys_getppid(struct registers* r);
extern void sys_threadnew(struct registers* r);
extern void sys_threadexit(struct registers* r);
extern void sys_gettid(struct registers* r);
extern void sys_open(struct registers* r);
extern void sys_mkdir(struct registers* r);
extern void sys_unlink(struct registers* r);
extern void sys_mount(struct registers* r);
extern void sys_unmount(struct registers* r);
extern void sys_close(struct registers* r);
extern void sys_read(struct registers* r);
extern void sys_write(struct registers* r);
extern void sys_ioctl(struct registers* r);
extern void sys_seek(struct registers* r);
extern void sys_truncate(struct registers* r);
extern void sys_poll(struct registers* r);
extern void sys_sync(struct registers* r);
extern void sys_getdents(struct registers* r);
extern void sys_stat(struct registers* r);
extern void sys_utime(struct registers* r);
extern void sys_chdir(struct registers* r);
extern void sys_fcntl(struct registers* r);
extern void sys_dup(struct registers* r);
extern void sys_mmap(struct registers* r);
extern void sys_munmap(struct registers* r);
extern void sys_mprotect(struct registers* r);
extern void sys_chroot(struct registers* r);
extern void sys_pipe(struct registers* r);
extern void sys_sleep(struct registers* r);
extern void sys_gettime(struct registers* r);
extern void sys_settime(struct registers* r);
extern void sys_uname(struct registers* r);
extern void sys_futex(struct registers* r);
extern void sys_poweroff(struct registers* r);
extern void sys_archctl(struct registers* r);

typedef void (*syscall_handler_t)(struct registers*);

static syscall_handler_t syscall_table[] = {
    [SYS_EXIT]          = sys_exit,
    [SYS_FORK]          = sys_fork,
    [SYS_EXEC]          = sys_exec,
    [SYS_WAIT]          = sys_wait,
    [SYS_GETPID]        = sys_getpid,
    [SYS_GETPPID]       = sys_getppid,
    [SYS_THREADNEW]     = sys_threadnew,
    [SYS_THREADEXIT]    = sys_threadexit,
    [SYS_GETTID]        = sys_gettid,
    [SYS_OPEN]          = sys_open,
    [SYS_MKDIR]         = sys_mkdir,
    [SYS_UNLINK]        = sys_unlink,
    [SYS_MOUNT]         = sys_mount,
    [SYS_UNMOUNT]       = sys_unmount,
    [SYS_CLOSE]         = sys_close,
    [SYS_READ]          = sys_read,
    [SYS_WRITE]         = sys_write,
    [SYS_IOCTL]         = sys_ioctl,
    [SYS_SEEK]          = sys_seek,
    [SYS_TRUNCATE]      = sys_truncate,
    [SYS_POLL]          = sys_poll,
    [SYS_SYNC]          = sys_sync,
    [SYS_GETDENTS]      = sys_getdents,
    [SYS_STAT]          = sys_stat,
    [SYS_UTIME]         = sys_utime,
    [SYS_CHDIR]         = sys_chdir,
    [SYS_FCNTL]         = sys_fcntl,
    [SYS_DUP]           = sys_dup,
    [SYS_MMAP]          = sys_mmap,
    [SYS_MUNMAP]        = sys_munmap,
    [SYS_MPROTECT]      = sys_mprotect,
    [SYS_CHROOT]        = sys_chroot,
    [SYS_PIPE]          = sys_pipe,
    [SYS_SLEEP]         = sys_sleep,
    [SYS_GETTIME]       = sys_gettime,
    [SYS_SETTIME]       = sys_settime,
    [SYS_UNAME]         = sys_uname,
    [SYS_FUTEX]         = sys_futex,
    [SYS_POWEROFF]      = sys_poweroff,
    [SYS_ARCHCTL]       = sys_archctl,
};

void syscall_handler(struct registers* r) {
    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        r->rax = -ENOSYS;
        return;
    }

    syscall_table[r->rax](r);
}
