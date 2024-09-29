#include <config.h>
#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/acpi/acpi.h>
#include <errno.h>
#include <sys/process.h>
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

    klog("[syscall] running syscall_utsname (utsname: 0x%p) on (pid: %u, tid: %u)\n",
            utsname, current_process->pid, current_thread->tid);

    if (copy_to_user(utsname, &system_utsname, sizeof(struct utsname)) == NULL) {
        r->rax = -EFAULT;
    } else {
        r->rax = 0;
    }
}

void syscall_sysact(struct registers* r) {
    uint64_t magic1 = r->rdi;
    uint64_t magic2 = r->rsi;
    uint64_t magic3 = r->rdx;
    int action = r->r10;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_sysact (magic1: 0x%lx, magic2: 0x%lx, magic3: 0x%lx, action: %d) on (pid: %u, tid: %u)\n",
            magic1, magic2, magic3, action, current_process->pid, current_thread->tid);

    if (magic1 != SYSACT_MAGIC1 || magic2 != SYSACT_MAGIC2 || magic3 != SYSACT_MAGIC3) {
        r->rax = -EPERM;
        return;
    }

    switch (action) {
        case SYSACT_HALT:
            cli();
            for (;;) {
                hlt();
            }
            break;
        case SYSACT_REBOOT:
            acpi_reboot();
            break;
        case SYSACT_SHUTDOWN:
            acpi_shutdown();
            break;
        default:
            r->rax = -EINVAL;
            return;
    }
}
