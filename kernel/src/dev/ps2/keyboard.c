#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/char/tty.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <dev/ps2.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/time.h>
#include <types.h>
#include <utils/panic.h>
#include <utils/ringbuf.h>
#include <utils/spinlock.h>

#define KEYBOARD_IRQ 1
#define KEYBUFFER_SIZE 256

READONLY_AFTER_INIT static char keymap_regular[] = {
    '\0', '\0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', '\0', 'a', 's', 'd', 'f', 'g', 'h',
    'j', 'k', 'l', ';', '\'', '`', '\0', '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.',  '/', '\0', '\0', '\0', ' '
};

READONLY_AFTER_INIT static char keymap_shift_nocaps[] = {
    '\0', '\0', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', '\0', 'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L', ':', '\"', '~', '\0', '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', '\0', '\0', '\0', ' '
};

READONLY_AFTER_INIT static char keymap_shift_caps[] = {
    '\0', '\0', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '{', '}', '\n',  '\0', 'a', 's', 'd', 'f', 'g', 'h',
    'j', 'k', 'l', ':', '\"', '~', '\0', '|', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', '<', '>',  '?', '\0', '\0', '\0', ' '
};

static char keymap_caps[] = {
    '\0', '\0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-','=', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '[', ']', '\n', '\0', 'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L', ';', '\'', '`', '\0', '\\', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', ',', '.', '/', '\0', '\0', '\0', ' '
};

static bool ctrl_active = false;
static bool shift_active = false;
static bool caps_lock = false;
static uint8_t led_state = 0;

static char translate_keyboard_scancode(uint8_t scancode) {
    bool release = scancode & 0x80;

    if (scancode == 0x1d || scancode == 0x9d) {
        ctrl_active = !release;
        return '\0';
    }

    if (scancode == 0x2a || scancode == 0x36 || scancode == 0xaa || scancode == 0xb6) {
        shift_active = !release;
        return '\0';
    }

    if (scancode == 0x3a) {
        caps_lock ^= 1;
        return '\0';
    }

    if (release) {
        return '\0';
    }

    char c = '\0';

    if (scancode < sizeof(keymap_regular)) {
        if (!caps_lock && !shift_active) {
            c = keymap_regular[scancode];
        } else if (!caps_lock && shift_active) {
            c = keymap_shift_nocaps[scancode];
        } else if (caps_lock && shift_active) {
            c = keymap_shift_caps[scancode];
        } else if (caps_lock && !shift_active) {
            c = keymap_caps[scancode];
        }
    }

    return c;
}

static void keyboard_irq_handler(struct registers* r, void* ctx) {
    (void) r;
    (void) ctx;

    uint8_t scancode = ps2_read_data();

    char c = translate_keyboard_scancode(scancode);
    if (c != '\0') {
        ringbuf_push(active_tty->input_buf, &c);
    }

    uint8_t new_led_state = led_state;
    if (scancode == 0x45) {
        new_led_state ^= (1 << 2);
    } else if (scancode == 0x3a) {
        new_led_state ^= (1 << 1);
    } else if (scancode == 0x46) {
        new_led_state ^= (1 << 0);
    }

    if (new_led_state != led_state) {
        ps2_send_device_command_with_data(PS2_DEVICE_COMMAND_KEYBOARD_SET_LED, new_led_state, false, false);
        led_state = new_led_state;
    }

    lapic_eoi();
}

UNMAP_AFTER_INIT void ps2_keyboard_init(void) {
    ps2_send_device_command_with_data(PS2_DEVICE_COMMAND_KEYBOARD_SET_LED, 7, false, false);
    ps2_send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, false);

    isr_install_handler(IRQ(KEYBOARD_IRQ), keyboard_irq_handler, NULL);

    ioapic_redirect_irq(KEYBOARD_IRQ, IRQ(KEYBOARD_IRQ));
    ioapic_set_irq_mask(KEYBOARD_IRQ, false);
}
