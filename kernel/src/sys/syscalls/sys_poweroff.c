#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h> 
#include <errno.h>
#include <sys/process.h>

#include <uacpi/sleep.h>

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
            break;
        case POWEROFF_REBOOT:
            cli();
            smp_halt_other_cpus();
            uacpi_reboot();
            break;
        case POWEROFF_SHUTDOWN:
            uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
            if (uacpi_unlikely_error(ret)) {
                r->rax = -EIO;
                return;
            }

            cli();
            smp_halt_other_cpus();

            uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
            break;
        default:
            r->rax = -EINVAL;
            return;
    }

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}
