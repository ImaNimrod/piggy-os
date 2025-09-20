#include <cpu/isr.h>
#include <dev/ioapic.h>
#include <stddef.h>
#include <utils/log.h>

#include "definitions.h"

static void ps2_mouse_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;

    klog("PS/2 mouse interrupt: 0x%02x\n", read_data());
}

void mouse_init(bool second_port) {
    isr_register_handler(PS2_MOUSE_ISA_IRQ + ISA_IRQ_BASE, ps2_mouse_irq_handler, NULL);
    ioapic_redirect_irq(PS2_MOUSE_ISA_IRQ, PS2_MOUSE_ISA_IRQ + ISA_IRQ_BASE);
    ioapic_set_irq_mask(PS2_MOUSE_ISA_IRQ, false);

    send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, second_port);

    klog("[ps2] PS/2 mouse initialized\n");
}
