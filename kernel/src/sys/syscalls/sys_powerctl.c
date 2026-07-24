#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h> 
#include <dev/acpi.h> 
#include <errno.h>
#include <sys/process.h>

#define POWERCTL_HALT       0x27baec8d
#define POWERCTL_REBOOT     0xce91fba2
#define POWERCTL_SHUTDOWN   0x19ba83ed

// TODO: ensure we poweroff safely in the future by unmounting filesystems, syncing block devices,
// killing processes, properly deiniting PCI devs, etc.
void sys_powerctl(struct registers* r) {
    int op = r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (current_process->pid != 1) {
        r->rax = -EPERM;
        return;
    }

    switch (op) {
        case POWERCTL_HALT:
            cli();
            smp_halt_other_cpus();

            for (;;) {
                hlt();
            }
            __builtin_unreachable();
        case POWERCTL_REBOOT:
            r->rax = acpi_reboot();
            break;
        case POWERCTL_SHUTDOWN:
            r->rax = acpi_shutdown();
            break;
        default:
            r->rax = -EINVAL;
            break;
    }
}
