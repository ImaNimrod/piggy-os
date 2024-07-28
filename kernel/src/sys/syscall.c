#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <errno.h>
#include <utils/log.h>
#include <utils/math.h>

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
extern void syscall_stat(struct registers* r);
extern void syscall_chdir(struct registers* r);
extern void syscall_getcwd(struct registers* r);
extern void syscall_utsname(struct registers* r);

static syscall_handler_t syscall_table[] = {
    syscall_exit,
    syscall_fork,
    syscall_exec,
    syscall_wait,
    syscall_yield,
    syscall_getpid,
    syscall_getppid,
    syscall_gettid,
    syscall_thread_create,
    syscall_thread_exit,
    syscall_sbrk,
    syscall_open,
    syscall_mkdir,
    syscall_close,
    syscall_read,
    syscall_write,
    syscall_ioctl,
    syscall_seek,
    syscall_truncate,
    syscall_stat,
    syscall_chdir,
    syscall_getcwd,
    syscall_utsname,
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
    this_cpu()->running_thread->stack = this_cpu()->user_stack;

    syscall_table[r->rax](r);
}
