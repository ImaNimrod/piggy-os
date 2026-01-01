#include <cpu/isr.h>
#include <dev/ioapic.h>
#include <stddef.h>
#include <utils/log.h>

#include "definitions.h"

static void ps2_mouse_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;
    flush();
}

void mouse_init(uint8_t irq) {
    isr_register_handler(irq + ISA_IRQ_BASE, ps2_mouse_irq_handler, NULL);
    ioapic_redirect_irq(irq, irq + ISA_IRQ_BASE);
    ioapic_set_irq_mask(irq, false);

    send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, true);

    send_device_command(0xff, true);

    klog("[ps2] PS/2 mouse initialized\n");
}
