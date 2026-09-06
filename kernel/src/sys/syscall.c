#include <cpu/asm.h>
#include <cpu/isr.h>
#include <errno.h>
#include <syscall.h>
#include <sys/signal.h>
#include <utils/macros.h>

extern void sys_exit(struct registers* r);
extern void sys_fork(struct registers* r);
extern void sys_exec(struct registers* r);
extern void sys_wait(struct registers* r);
extern void sys_kill(struct registers* r);
extern void sys_getpid(struct registers* r);
extern void sys_getppid(struct registers* r);
extern void sys_setpgid(struct registers* r);
extern void sys_getpgid(struct registers* r);
extern void sys_threadnew(struct registers* r);
extern void sys_threadexit(struct registers* r);
extern void sys_gettid(struct registers* r);
extern void sys_yield(struct registers* r);
extern void sys_open(struct registers* r);
extern void sys_mkdir(struct registers* r);
extern void sys_rename(struct registers* r);
extern void sys_link(struct registers* r);
extern void sys_symlink(struct registers* r);
extern void sys_readlink(struct registers* r);
extern void sys_unlink(struct registers* r);
extern void sys_mount(struct registers* r);
extern void sys_unmount(struct registers* r);
extern void sys_close(struct registers* r);
extern void sys_read(struct registers* r);
extern void sys_write(struct registers* r);
extern void sys_pread(struct registers* r);
extern void sys_pwrite(struct registers* r);
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
extern void sys_getclock(struct registers* r);
extern void sys_getclockres(struct registers* r);
extern void sys_setclock(struct registers* r);
extern void sys_sigaction(struct registers* r);
extern void sys_sigaltstack(struct registers* r);
extern void sys_sigpending(struct registers* r);
extern void sys_sigprocmask(struct registers* r);
extern void sys_sigreturn(struct registers* r);
extern void sys_sigsuspend(struct registers* r);
extern void sys_socket(struct registers* r);
extern void sys_bind(struct registers* r);
extern void sys_connect(struct registers* r);
extern void sys_recv(struct registers* r);
extern void sys_send(struct registers* r);
extern void sys_getsockname(struct registers* r);
extern void sys_getpeername(struct registers* r);
extern void sys_shutdown(struct registers* r);
extern void sys_sethostname(struct registers* r);
extern void sys_sysinfo(struct registers* r);
extern void sys_futex(struct registers* r);
extern void sys_procctl(struct registers* r);
extern void sys_powerctl(struct registers* r);
extern void sys_archctl(struct registers* r);

typedef void (*syscall_handler_t)(struct registers*);

static syscall_handler_t syscall_table[] = {
    [SYS_EXIT]          = sys_exit,
    [SYS_FORK]          = sys_fork,
    [SYS_EXEC]          = sys_exec,
    [SYS_WAIT]          = sys_wait,
    [SYS_KILL]          = sys_kill,
    [SYS_GETPID]        = sys_getpid,
    [SYS_GETPPID]       = sys_getppid,
    [SYS_GETPGID]       = sys_getpgid,
    [SYS_SETPGID]       = sys_setpgid,
    [SYS_THREADNEW]     = sys_threadnew,
    [SYS_THREADEXIT]    = sys_threadexit,
    [SYS_GETTID]        = sys_gettid,
    [SYS_OPEN]          = sys_open,
    [SYS_MKDIR]         = sys_mkdir,
    [SYS_RENAME]        = sys_rename,
    [SYS_LINK]          = sys_link,
    [SYS_SYMLINK]       = sys_symlink,
    [SYS_READLINK]      = sys_readlink,
    [SYS_UNLINK]        = sys_unlink,
    [SYS_MOUNT]         = sys_mount,
    [SYS_UNMOUNT]       = sys_unmount,
    [SYS_CLOSE]         = sys_close,
    [SYS_READ]          = sys_read,
    [SYS_WRITE]         = sys_write,
    [SYS_PREAD]         = sys_pread,
    [SYS_PWRITE]        = sys_pwrite,
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
    [SYS_GETCLOCK]      = sys_getclock,
    [SYS_GETCLOCKRES]   = sys_getclockres,
    [SYS_SETCLOCK]      = sys_setclock,
    [SYS_SIGACTION]     = sys_sigaction,
    [SYS_SIGALTSTACK]   = sys_sigaltstack,
    [SYS_SIGPENDING]    = sys_sigpending,
    [SYS_SIGPROCMASK]   = sys_sigprocmask,
    [SYS_SIGRETURN]     = sys_sigreturn,
    [SYS_SIGSUSPEND]    = sys_sigsuspend,
    [SYS_SOCKET]        = sys_socket,
    [SYS_BIND]          = sys_bind,
    [SYS_CONNECT]       = sys_connect,
    [SYS_RECV]          = sys_recv,
    [SYS_SEND]          = sys_send,
    [SYS_GETSOCKNAME]   = sys_getsockname,
    [SYS_GETPEERNAME]   = sys_getpeername,
    [SYS_SHUTDOWN]      = sys_shutdown,
    [SYS_SETHOSTNAME]   = sys_sethostname,
    [SYS_SYSINFO]       = sys_sysinfo,
    [SYS_FUTEX]         = sys_futex,
    [SYS_PROCCTL]       = sys_procctl,
    [SYS_POWERCTL]      = sys_powerctl,
    [SYS_ARCHCTL]       = sys_archctl,
};

void syscall_handler(struct registers* r) {
    signal_handle_pending(r);

    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        r->rax = -ENOSYS;
    } else {
        syscall_table[r->rax](r);
    }
}
