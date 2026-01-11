#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h> 
#include <dev/acpi.h> 
#include <errno.h>

#define POWEROFF_HALT       0x27baec8d
#define POWEROFF_REBOOT     0xce91fba2
#define POWEROFF_SHUTDOWN   0x19ba83ed

// TODO: ensure we poweroff safely in the future by unmounting filesystems, syncing block devices,
// killing processes, properly deiniting PCI devs, etc.
void sys_poweroff(struct registers* r) {
    int how = r->rdi;

    switch (how) {
        case POWEROFF_HALT:
            cli();
            smp_halt_other_cpus();

            for (;;) {
                hlt();
            }
            __builtin_unreachable();
        case POWEROFF_REBOOT:
            r->rax = acpi_reboot();
            break;
        case POWEROFF_SHUTDOWN:
            r->rax = acpi_shutdown();
            break;
        default:
            r->rax = -EINVAL;
            break;
    }
}
