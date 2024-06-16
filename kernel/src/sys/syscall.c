#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <utils/log.h>
#include <utils/math.h>

struct syscall_handle {
    void (*handler)(struct registers* r);
    const char* name;
};

extern void syscall_fork(struct registers* r);
extern void syscall_exit(struct registers* r);
extern void syscall_yield(struct registers* r);
extern void syscall_getpid(struct registers* r);
extern void syscall_gettid(struct registers* r);
extern void syscall_exit_thread(struct registers* r);

static void syscall_test(struct registers* r) {
    (void) r;
    klog("syscall test\n");
    r->rax = 69420;
}

static struct syscall_handle syscall_table[] = {
    { .handler = syscall_test, .name = "test" },
    { .handler = syscall_fork, .name = "fork" },
    { .handler = syscall_exit, .name = "exit" },
    { .handler = syscall_yield, .name = "yield" },
    { .handler = syscall_getpid, .name = "getpid" },
    { .handler = syscall_gettid, .name = "gettid" },
};

void syscall_handler(struct registers* r) {
    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        klog("[syscall] unknown syscall number: %u\n", r->rax);
        r->rax = -1;
        return;
    }

    struct syscall_handle syscall = syscall_table[r->rax];

    if (!syscall.handler) {
        kpanic(r, "null syscall %s\n", syscall.name);
    } else {
        klog("[syscall] running syscall %s (pid %d, tid %d)\n",
                syscall.name, this_cpu()->running_thread->tid, this_cpu()->running_thread->process->pid);
        syscall.handler(r);
    }
}
