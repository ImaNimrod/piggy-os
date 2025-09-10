#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/syscall.h>
#include <utils/macros.h>

extern void syscall_exit(struct registers* r);
extern void syscall_fork(struct registers* r);
extern void syscall_exec(struct registers* r);
extern void syscall_getpid(struct registers* r);
extern void syscall_getppid(struct registers* r);
extern void syscall_gettid(struct registers* r);

typedef void (*syscall_handler_t)(struct registers*);

static syscall_handler_t syscall_table[] = {
    [SYS_EXIT]      = syscall_exit,
    [SYS_FORK]      = syscall_fork,
    [SYS_EXEC]      = syscall_exec,
    [SYS_GETPID]    = syscall_getpid,
    [SYS_GETPPID]   = syscall_getppid,
    [SYS_GETTID]    = syscall_gettid,
};

void syscall_handler(struct registers* r) {
    if (r->rax >= SIZEOF_ARRAY(syscall_table)) {
        r->rax = -ENOSYS;
        return;
    }

    // TODO: actually utilize SMAP correctly and make safe to/from user copy functions
    if (this_cpu()->has_smap) {
        stac();
    }

    syscall_table[r->rax](r);

    if (this_cpu()->has_smap) {
        clac();
    }
}
