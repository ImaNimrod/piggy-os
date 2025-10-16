#include <cpu/isr.h>
#include <dev/char/tty.h>
#include <dev/ioapic.h>
#include <stddef.h>
#include <utils/log.h>

#include "definitions.h"

static const char keymap_normal[] = {
    '\0', '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', '\0', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '\0', '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', '\0', '\0', '\0', ' ',
};

static const char keymap_shift[] = {
    '\0', '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', '\0', 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', '\0', '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', '\0', '\0', '\0', ' ',
};

static const char keymap_capslock[] = {
    '\0', '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', '\n', '\0', 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`', '\0', '\\', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', ',', '.', '/', '\0', '\0', '\0', ' ',
};

static const char keymap_shift_capslock[] = {
    '\0', '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '{', '}', '\n', '\0', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ':', '"', '~', '\0', '|', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', '<', '>', '?', '\0', '\0', '\0', ' ',
};

static bool is_second_port;

static bool shift_active;
static bool capslock_active;
static bool ctrl_active;
static uint8_t led_state;

static char translate_scancode(uint8_t scancode) {
    bool release = scancode & (1 << 7);
    scancode &= ~(1 << 7);

    if (scancode == 0x1d) {
        ctrl_active = !release;
        return '\0';
    }

    if (scancode == 0x2a || scancode == 0x36) {
        shift_active = !release;
        return '\0';
    }

    if (scancode == 0x3a && !release) {
        capslock_active = !capslock_active;
        return '\0';
    }

    if (release) {
        return '\0';
    }

    char c = '\0';

    if (scancode < sizeof(keymap_normal)) {
        if (!capslock_active && !shift_active) {
            c = keymap_normal[scancode];
        }
        if (shift_active && !capslock_active) {
            c = keymap_shift[scancode];
        }
        if (!shift_active && capslock_active) {
            c = keymap_capslock[scancode];
        }
        if (shift_active && capslock_active) {
            c = keymap_shift_capslock[scancode];
        }
    }

    return c;
}

static void ps2_keyboard_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;

    if (unlikely(!tty_is_ready)) {
        flush();
        return;
    }

    for (;;) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if (!(status & (1 << 0))) {
            break;
        }
        if (status & (1 << 5)) {
            continue;
        }

        uint8_t scancode = inb(PS2_DATA_PORT);

        char c = translate_scancode(scancode);

        uint8_t new_led_state = led_state;
        switch (scancode) {
            case 0x3a:
                new_led_state ^= (1 << 1);
                break;
            case 0x45:
                new_led_state ^= (1 << 2);
                break;
            case 0x46:
                new_led_state ^= (1 << 0);
                break;
        }

        if (new_led_state != led_state) {
            send_device_command_with_data(PS2_KEYBOARD_COMMAND_SET_LEDS, new_led_state, is_second_port);
            led_state = new_led_state;
        }

        if (tty_is_ready && c != '\0') {
            tty_add_char(c);
        }
    }
}

void keyboard_init(bool second_port) {
    is_second_port = second_port;

    isr_register_handler(PS2_KEYBOARD_ISA_IRQ + ISA_IRQ_BASE, ps2_keyboard_irq_handler, NULL);
    ioapic_redirect_irq(PS2_KEYBOARD_ISA_IRQ, PS2_KEYBOARD_ISA_IRQ + ISA_IRQ_BASE);
    ioapic_set_irq_mask(PS2_KEYBOARD_ISA_IRQ, false);

    send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, second_port);

    klog("[ps2] PS/2 keyboard initialized\n");
}
