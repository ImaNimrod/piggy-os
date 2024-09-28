#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <errno.h>
#include <utils/log.h>
#include <utils/math.h>

#define SYS_EXIT            0
#define SYS_FORK            1
#define SYS_EXEC            2
#define SYS_WAIT            3
#define SYS_YIELD           4
#define SYS_GETPID          5
#define SYS_GETPPID         6
#define SYS_GETTID          7
#define SYS_THREAD_CREATE   8
#define SYS_THREAD_EXIT     9
#define SYS_SBRK            10
#define SYS_OPEN            11
#define SYS_MKDIR           12
#define SYS_CLOSE           13
#define SYS_READ            14
#define SYS_WRITE           15
#define SYS_IOCTL           16
#define SYS_SEEK            17
#define SYS_TRUNCATE        18
#define SYS_FCNTL           19
#define SYS_FSYNC           20
#define SYS_STAT            21
#define SYS_CHDIR           22
#define SYS_GETCWD          23
#define SYS_GETDENTS        24
#define SYS_UTSNAME         25
#define SYS_SYSACT          26
#define SYS_SLEEP           27
#define SYS_CLOCK_GETTIME   28
#define SYS_CLOCK_SETTIME   29

typedef void (*syscall_handler_t)(struct registers*);

extern void syscall_exit(struct registers* r);
extern void syscall_fork(struct registers* r);
extern void syscall_exec(struct registers* r);
extern void syscall_wait(struct registers* r);
extern void syscall_yield(struct registers* r);
extern void syscall_getpid(struct registers* r);
extern void syscall_getppid(struct registers* r);
extern void syscall_gettid(struct registers* r);
extern void syscall_thread_create(struct registers* r);
extern void syscall_thread_exit(struct registers* r);
extern void syscall_sbrk(struct registers* r);
extern void syscall_open(struct registers* r);
extern void syscall_mkdir(struct registers* r);
extern void syscall_close(struct registers* r);
extern void syscall_read(struct registers* r);
extern void syscall_write(struct registers* r);
extern void syscall_ioctl(struct registers* r);
extern void syscall_seek(struct registers* r);
extern void syscall_truncate(struct registers* r);
extern void syscall_fcntl(struct registers* r);
extern void syscall_fsync(struct registers* r);
extern void syscall_stat(struct registers* r);
extern void syscall_chdir(struct registers* r);
extern void syscall_getcwd(struct registers* r);
extern void syscall_getdents(struct registers* r);
extern void syscall_utsname(struct registers* r);
extern void syscall_sysact(struct registers* r);
extern void syscall_sleep(struct registers* r);
extern void syscall_clock_gettime(struct registers* r);
extern void syscall_clock_settime(struct registers* r);

static syscall_handler_t syscall_table[] = {
    [SYS_EXIT]          = syscall_exit,
    [SYS_FORK]          = syscall_fork,
    [SYS_EXEC]          = syscall_exec,
    [SYS_WAIT]          = syscall_wait,
    [SYS_YIELD]         = syscall_yield,
    [SYS_GETPID]        = syscall_getpid,
    [SYS_GETPPID]       = syscall_getppid,
    [SYS_GETTID]        = syscall_gettid,
    [SYS_THREAD_CREATE] = syscall_thread_create,
    [SYS_THREAD_EXIT]   = syscall_thread_exit,
    [SYS_SBRK]          = syscall_sbrk,
    [SYS_OPEN]          = syscall_open,
    [SYS_MKDIR]         = syscall_mkdir,
    [SYS_CLOSE]         = syscall_close,
    [SYS_READ]          = syscall_read,
    [SYS_WRITE]         = syscall_write,
    [SYS_IOCTL]         = syscall_ioctl,
    [SYS_SEEK]          = syscall_seek,
    [SYS_TRUNCATE]      = syscall_truncate,
    [SYS_FCNTL]         = syscall_fcntl,
    [SYS_FSYNC]         = syscall_fsync,
    [SYS_STAT]          = syscall_stat,
    [SYS_CHDIR]         = syscall_chdir,
    [SYS_GETCWD]        = syscall_getcwd,
    [SYS_GETDENTS]      = syscall_getdents,
    [SYS_UTSNAME]       = syscall_utsname,
    [SYS_SYSACT]        = syscall_sysact,
    [SYS_SLEEP]         = syscall_sleep,
    [SYS_CLOCK_GETTIME] = syscall_clock_gettime,
    [SYS_CLOCK_SETTIME] = syscall_clock_settime,
};

void syscall_handler(struct registers* r) {
    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        klog("[syscall] unknown syscall number: %u\n", r->rax);
        r->rax = -ENOSYS;
        return;
    }

    if (this_cpu()->smap_enabled) {
        clac(); // just sanity check that user memory access is disabled
    }

    this_cpu()->running_thread->ctx = *r;
    this_cpu()->running_thread->user_stack = this_cpu()->user_stack;
    this_cpu()->fpu_save(this_cpu()->running_thread->fpu_storage);

    syscall_table[r->rax](r);
}
