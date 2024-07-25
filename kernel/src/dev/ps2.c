#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/acpi/acpi.h>
#include <dev/char/tty.h>
#include <dev/ioapic.h>
#include <dev/lapic.h>
#include <dev/ps2.h>
#include <utils/log.h>
#include <utils/spinlock.h>

#define KEYBOARD_IRQ 1

#define PS2_DATA_PORT       0x60
#define PS2_COMMAND_PORT    0x64
#define PS2_STATUS_PORT     0x64

#define PS2_COMMAND_READ_CONFIG         0x20
#define PS2_COMMAND_WRITE_CONFIG        0x60
#define PS2_COMMAND_TEST_PORT1          0xab
#define PS2_COMMAND_DISABLE_PORT1       0xad
#define PS2_COMMAND_ENABLE_PORT1        0xae
#define PS2_COMMAND_DISABLE_PORT2       0xa7
#define PS2_COMMAND_ENABLE_PORT2        0xa8
#define PS2_COMMAND_TEST_PORT2          0xa9
#define PS2_COMMAND_SELF_TEST           0xaa
#define PS2_COMMAND_SEND_TO_SECOND_PORT 0xd4

#define PS2_DEVICE_COMMAND_KEYBOARD_SET_LED 0xed
#define PS2_DEVICE_COMMAND_DISABLE_SCANNING 0xf5
#define PS2_DEVICE_COMMAND_ENABLE_SCANNING  0xf4
#define PS2_DEVICE_COMMAND_IDENTIFY         0xf2
#define PS2_DEVICE_COMMAND_RESET            0xff

static char keymap_regular[] = {
    '\0', '\0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', '\0', 'a', 's', 'd', 'f', 'g', 'h',
    'j', 'k', 'l', ';', '\'', '`', '\0', '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.',  '/', '\0', '\0', '\0', ' '
};

static char keymap_shift_nocaps[] = {
    '\0', '\0', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', '\0', 'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L', ':', '\"', '~', '\0', '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', '\0', '\0', '\0', ' '
};

static char keymap_shift_caps[] = {
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

static uint8_t read_data(void) {
    while (!(inb(PS2_STATUS_PORT) & 1)) {
        pause();
    }
    return inb(PS2_DATA_PORT);
}

static void send_command(uint8_t command) {
    while (inb(PS2_STATUS_PORT) & 2) {
        pause();
    }
    outb(PS2_COMMAND_PORT, command);
}

static void send_data(uint8_t data) {
    while (inb(PS2_STATUS_PORT) & 2) {
        pause();
    }
    outb(PS2_DATA_PORT, data);
}

static void flush_buffer(void) {
    while (inb(PS2_STATUS_PORT) & 1) {
        inb(PS2_DATA_PORT);
    }
}

static uint8_t send_device_command(uint8_t command, bool second_port) {
    uint8_t res;

    for (int i = 0; i < 3; i++) {
        if (second_port) {
            send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        send_data(command);

        res = read_data();
        if (res != 0xfe) {
            return res;
        }
    }

    return res;
}

static uint8_t send_device_command_with_data(uint8_t command, uint8_t data, bool ack_before_data, bool second_port) {
    uint8_t res;

    for (int i = 0; i < 3; i++) {
        if (second_port) {
            send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        send_data(command);

        if (ack_before_data) {
            res = read_data();

            if (res == 0xfe) {
                continue;
            }
            if (res != 0xfa) {
                return res;
            }
        }

        if (second_port) {
            send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        send_data(data);

        res = read_data();
        if (res != 0xfe) {
            return res;
        }
    }

    return res;
}

static void keyboard_irq_handler(struct registers* r) {
    (void) r;

    if (!active_tty) {
        flush_buffer();
        lapic_eoi();
        return;
    }

    uint8_t scancode = read_data();

    uint8_t new_led_state = led_state;
    if (scancode == 0x45) {
        new_led_state ^= 2;
    } else if (scancode == 0x3a) {
        new_led_state ^= 4;
    } else if (scancode == 0x46) {
        new_led_state ^= 1;
    }

    if (new_led_state != led_state) {
        send_device_command_with_data(PS2_DEVICE_COMMAND_KEYBOARD_SET_LED, new_led_state, false, false);
        led_state = new_led_state;
    }

    char c = translate_keyboard_scancode(scancode);
    if (c != '\0') {
        ringbuf_push(active_tty->input_buf, &c);
    }

    lapic_eoi();
}

static bool port_enum_and_init_device(bool second_port) {
    if (send_device_command(PS2_DEVICE_COMMAND_RESET, second_port) != 0xfa) {
        klog("bruh\n");
        return false;
    }
    if (read_data() != 0xaa) {
        return false;
    }

    flush_buffer();

    if (send_device_command(PS2_DEVICE_COMMAND_DISABLE_SCANNING, second_port) != 0xfa) {
        return false;
    }
    if (send_device_command(PS2_DEVICE_COMMAND_IDENTIFY, second_port) != 0xfa) {
        return false;
    }

    uint8_t id = read_data();
    if (id == 0xab) {
        id = read_data();
        if (id == 0x41 || id == 0x83 || id == 0xc1) {
            klog("[ps2] PS/2 keyboard detected\n");
            isr_install_handler(IRQ(KEYBOARD_IRQ), true, keyboard_irq_handler);
            ioapic_set_irq_mask(KEYBOARD_IRQ, false);
        }
    } else if (id == 0x00 || id == 0x03 || id == 0x04) {
        klog("[ps2] PS/2 mouse detected\n");
    } else {
        return false;
    }

    return true;
}

void ps2_init(void) {
    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (fadt != NULL) {
        uint16_t iapc_boot_arch_flags = *(uint16_t*) ((uintptr_t) fadt + 109);
        if (!(iapc_boot_arch_flags & (1 << 1))) {
            klog("[ps2] system lacks a PS/2 controller\n");
            return;
        }
    }

    send_command(PS2_COMMAND_DISABLE_PORT1);
    send_command(PS2_COMMAND_DISABLE_PORT2);

    flush_buffer();

    send_command(PS2_COMMAND_READ_CONFIG);
    uint8_t config = read_data();
    config &= ~((1 << 0) | (1 << 1) | (1 << 6));
    send_command(PS2_COMMAND_WRITE_CONFIG);
    send_data(config);

    send_command(PS2_COMMAND_SELF_TEST);
    uint8_t result = read_data();
    if (result != 0x55) {
        klog("[ps2] PS/2 controller self test failed\n");
        return;
    }

    bool two_channels = false;

    if (config & (1 << 5)) {
        send_command(PS2_COMMAND_ENABLE_PORT2);

        send_command(PS2_COMMAND_READ_CONFIG);
        if (!(read_data() & (1 << 5))) {
            two_channels = true;
        }
    }

    send_command(PS2_COMMAND_TEST_PORT1);
    bool have_port1 = read_data() == 0;
    send_command(PS2_COMMAND_TEST_PORT2);
    bool have_port2 = two_channels && read_data() == 0;

    if (!have_port1 && !have_port2) {
        klog("[ps2] no usable PS/2 ports detected\n");
        return;
    }

    if (have_port1) {
        send_command(PS2_COMMAND_ENABLE_PORT1);
    }
    if (have_port2) {
        send_command(PS2_COMMAND_ENABLE_PORT2);
    }

    send_command(PS2_COMMAND_READ_CONFIG);
    config = read_data();

    if (have_port1) {
        config |= (1 << 0);
    }
    if (have_port2) {
        config |= (1 << 1);
    }

    config |= (1 << 6);

    send_command(PS2_COMMAND_WRITE_CONFIG);
    send_data(config);

    bool have_device1 = false;
    if (have_port1) {
        have_device1 = port_enum_and_init_device(false);
    }
    bool have_device2 = false;
    if (have_port2) {
        have_device2 = port_enum_and_init_device(true);
    }

    if (have_device1) {
        send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, false);
    }
    if (have_device2) {
        send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, true);
    }
}
