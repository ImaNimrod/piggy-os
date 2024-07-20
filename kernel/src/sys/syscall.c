#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <utils/log.h>
#include <utils/math.h>

struct syscall_handle {
    void (*handler)(struct registers* r);
    const char* name;
};

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
extern void syscall_close(struct registers* r);
extern void syscall_read(struct registers* r);
extern void syscall_write(struct registers* r);
extern void syscall_ioctl(struct registers* r);
extern void syscall_seek(struct registers* r);
extern void syscall_truncate(struct registers* r);
extern void syscall_stat(struct registers* r);
extern void syscall_chdir(struct registers* r);
extern void syscall_getcwd(struct registers* r);

static struct syscall_handle syscall_table[] = {
    { .handler = syscall_exit, .name = "exit" },
    { .handler = syscall_fork, .name = "fork" },
    { .handler = syscall_exec, .name = "exec" },
    { .handler = syscall_wait, .name = "wait" },
    { .handler = syscall_yield, .name = "yield" },
    { .handler = syscall_getpid, .name = "getpid" },
    { .handler = syscall_getppid, .name = "getppid" },
    { .handler = syscall_gettid, .name = "gettid" },
    { .handler = syscall_thread_create, .name = "thread_create" },
    { .handler = syscall_thread_exit, .name = "thread_exit" },
    { .handler = syscall_sbrk, .name = "sbrk" },
    { .handler = syscall_open, .name = "open" },
    { .handler = syscall_close, .name = "close" },
    { .handler = syscall_read, .name = "read" },
    { .handler = syscall_write, .name = "write" },
    { .handler = syscall_ioctl, .name = "ioctl" },
    { .handler = syscall_seek, .name = "seek" },
    { .handler = syscall_truncate, .name = "truncate" },
    { .handler = syscall_stat, .name = "stat" },
    { .handler = syscall_chdir, .name = "chdir" },
    { .handler = syscall_getcwd, .name = "getcwd" },
};

void syscall_handler(struct registers* r) {
    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        klog("[syscall] unknown syscall number: %u\n", r->rax);
        r->rax = -1;
        return;
    }

    struct syscall_handle syscall = syscall_table[r->rax];

	this_cpu()->running_thread->ctx = *r;
	this_cpu()->running_thread->stack = this_cpu()->user_stack;

    if (!syscall.handler) {
        kpanic(r, "null syscall %s\n", syscall.name);
    } else {
        klog("[syscall] running syscall %s (pid %d, tid %d)\n",
                syscall.name, this_cpu()->running_thread->process->pid, this_cpu()->running_thread->tid);
        syscall.handler(r);
    }
}
