#include <cpu/ioapic.h>
#include <cpu/isr.h>
#include <dev/char/tty.h>
#include <fs/devfs.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>
#include <utils/usercopy.h>

#include "definitions.h"

#define KEYBOARD_DEV_MAJOR 6

#define SCANCODE_BUF_SIZE 128

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

static const char keymap_control[] = {
    '\0', '\033', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\b', '\t',
    0x11, 0x17, 0x05, 0x12, 0x14, 0x19, 0x15, 0x09, 0x0f, 0x10, '\0', '\0', '\n', '\0', 0x01, 0x13,
    0x04, 0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0c, '\0', '\0', '\0', '\0', '\0', 0x1a, 0x18, 0x03, 0x16,
    0x02, 0x0e, 0x0d, '\0', '\0', '\0', '\0', '\0', '\0', ' ',
};

static bool shift_active;
static bool capslock_active;
static bool ctrl_active;
static uint8_t led_state;

static uint8_t* scancode_buf;
static size_t scancode_buf_index;
static spinlock_t scancode_buf_lock;

static ssize_t keyboard_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);

static struct device_ops keyboard_ops = {
    .read = keyboard_read,
};

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
        if (ctrl_active) {
            c = keymap_control[scancode];
        } else {
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
    }

    return c;
}

static ssize_t keyboard_read(dev_t dev, void* buf, size_t count, off_t offset, int flags) {
    (void) dev;
    (void) offset;

    if (count == 0) {
        return 0;
    }

    ssize_t to_copy;
    if (flags & O_NONBLOCK) {
        to_copy = MIN(count, scancode_buf_index);
        if (to_copy == 0) {
            return 0;
        }
    } else {
        to_copy = count;
        while ((ssize_t) scancode_buf_index != to_copy) {
            scheduler_yield(true);
        }
    }

    spinlock_acquire(&scancode_buf_lock);

    ssize_t ret;
    if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf, scancode_buf, to_copy)) < 0) {
        spinlock_release(&scancode_buf_lock);
        return ret;
    }

    memmove(scancode_buf, scancode_buf + to_copy, scancode_buf_index - to_copy);
    scancode_buf_index -= to_copy;

    spinlock_release(&scancode_buf_lock);
    return to_copy;
}

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

        spinlock_acquire(&scancode_buf_lock);
        scancode_buf[scancode_buf_index++] = scancode;
        spinlock_release(&scancode_buf_lock);

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
            send_device_command_with_data(PS2_KEYBOARD_COMMAND_SET_LEDS, new_led_state, false);
            led_state = new_led_state;
        }

        if (tty_is_ready && c != '\0') {
            tty_add_char(c);
        }
    }
}

void keyboard_init(uint8_t irq) {
    scancode_buf = kmalloc(SCANCODE_BUF_SIZE * sizeof(uint8_t));
    if (unlikely(scancode_buf == NULL)) {
        kpanic(NULL, false, "failed to create keyboard device scancode buffer");
    }

    isr_register_handler(irq + ISA_IRQ_BASE, ps2_keyboard_irq_handler, NULL);
    ioapic_redirect_irq(irq, irq + ISA_IRQ_BASE);
    ioapic_set_irq_mask(irq, false);

    if (unlikely(devfs_register("kbd", VFS_TYPE_CHARDEV, &keyboard_ops, makedev(KEYBOARD_DEV_MAJOR, 0)) < 0)) {
        kpanic(NULL, false, "failed to create keyboard device");
    }

    klog("[ps2] PS/2 keyboard initialized\n");
}
