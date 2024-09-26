#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/char/tty.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <dev/ps2.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/time.h>
#include <types.h>
#include <utils/panic.h>
#include <utils/ringbuf.h>
#include <utils/spinlock.h>

#define KEYBOARD_IRQ 1
#define KEYBUFFER_SIZE 256

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

static uint8_t* key_buffer = NULL;
static uint32_t key_buffer_write_ptr = 0;
static uint32_t key_buffer_read_ptr = 0;

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

    for (;;) {
        uint8_t status = ps2_read_status();
        if (!(status & (1 << 0))) {
            break;
        }
        if (status & (1 << 5)) {
            continue;
        }

        uint8_t scancode = ps2_read_data();

        key_buffer[key_buffer_write_ptr] = scancode;
        key_buffer_write_ptr = (key_buffer_write_ptr + 1) % KEYBUFFER_SIZE;

        char c = translate_keyboard_scancode(scancode);
        if (c != '\0') {
            ringbuf_push(active_tty->input_buf, &c);
        }

        uint8_t new_led_state = led_state;
        if (scancode == 0x45) {
            new_led_state ^= 2;
        } else if (scancode == 0x3a) {
            new_led_state ^= 4;
        } else if (scancode == 0x46) {
            new_led_state ^= 1;
        }

        if (new_led_state != led_state) {
            ps2_send_device_command_with_data(PS2_DEVICE_COMMAND_KEYBOARD_SET_LED, new_led_state, false, false);
            led_state = new_led_state;
        }
    }


    lapic_eoi();
}

static ssize_t keyboard_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) node;
    (void) offset;

    uint8_t* buf_u8 = (uint8_t*) buf;
    size_t actual_count = count;

    while (actual_count--) {
        if (flags & O_NONBLOCK && key_buffer_write_ptr == key_buffer_read_ptr) {
            return 0;
        }

        while (key_buffer_read_ptr == key_buffer_write_ptr) {
            pause();
        }

        *buf_u8 = key_buffer[key_buffer_read_ptr];
        key_buffer_read_ptr = (key_buffer_read_ptr + 1) % KEYBUFFER_SIZE;
        buf_u8++;
    }

    return count;
}

void ps2_keyboard_init(void) {
    ps2_send_device_command_with_data(PS2_DEVICE_COMMAND_KEYBOARD_SET_LED, 0, false, false);
    ps2_send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, false);

    isr_install_handler(IRQ(KEYBOARD_IRQ), keyboard_irq_handler, NULL);

    ioapic_redirect_irq(KEYBOARD_IRQ, IRQ(KEYBOARD_IRQ));
    ioapic_set_irq_mask(KEYBOARD_IRQ, false);

    key_buffer = kmalloc(KEYBUFFER_SIZE * sizeof(uint8_t));
    if (key_buffer == NULL) {
        kpanic(NULL, false, "failed to create key buffer for PS/2 keyboard device");
    }

    struct vfs_node* kbd_node = devfs_create_device("keyboard");
    if (kbd_node == NULL) {
        kpanic(NULL, false, "failed to create device node for PS/2 keyboard in devfs");
    }

    kbd_node->stat = (struct stat) {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_rdev = makedev(KBDDEV_MAJ, 0),
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
        .st_atim = time_realtime,
        .st_ctim = time_realtime,
        .st_mtim = time_realtime
    };

    kbd_node->read = keyboard_read;
}
