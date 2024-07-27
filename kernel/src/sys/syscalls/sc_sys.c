#include <config.h>
#include <cpu/isr.h>
#include <errno.h>
#include <types.h>
#include <utils/log.h>
#include <utils/user_access.h>

static struct utsname system_utsname = {
    .sysname = CONFIG_SYSNAME,
    .nodename = "piggy",
    .release = CONFIG_RELEASE,
    .version = CONFIG_VERSION,
    .machine = "x86_64"
};

void syscall_utsname(struct registers* r) {
    struct utsname* utsname = (struct utsname*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_utsname (utsname: 0x%x) on (pid: %u, tid: %u)\n",
            (uintptr_t) utsname, current_process->pid, current_thread->tid);

    if (!check_user_ptr(utsname)) {
        r->rax = -EFAULT;
        return;
    }

    copy_to_user(utsname, &system_utsname, sizeof(struct utsname));
    r->rax = 0;
}
