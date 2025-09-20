#include <cpu/isr.h>
#include <dev/ioapic.h>
#include <stddef.h>
#include <utils/log.h>

#include "definitions.h"

static void ps2_keyboard_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;

    for (;;) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if (!(status & (1 << 0))) {
            break;
        }
        if (status & (1 << 5)) {
            continue;
        }

        uint8_t scancode = inb(PS2_DATA_PORT);
        klog("received scancode: 0x%02x\n", scancode);
    }
}

void keyboard_init(bool second_port) {
    isr_register_handler(PS2_KEYBOARD_ISA_IRQ + ISA_IRQ_BASE, ps2_keyboard_irq_handler, NULL);
    ioapic_redirect_irq(PS2_KEYBOARD_ISA_IRQ, PS2_KEYBOARD_ISA_IRQ + ISA_IRQ_BASE);
    ioapic_set_irq_mask(PS2_KEYBOARD_ISA_IRQ, false);

    send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, second_port);

    klog("[ps2] PS/2 keyboard initialized\n");
}
