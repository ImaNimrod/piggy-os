#include <cpu/asm.h>
#include <dev/acpi/acpi.h>
#include <dev/ps2.h>
#include <utils/log.h>

#define PS2_DATA_PORT       0x60
#define PS2_COMMAND_PORT    0x64
#define PS2_STATUS_PORT     0x64

__attribute__((section(".unmap_after_init")))
static void port_enum_and_init_device(bool second_port) {
    if (ps2_send_device_command(PS2_DEVICE_COMMAND_RESET, second_port) != 0xfa) {
        return;
    }
    if (ps2_read_data() != 0xaa) {
        return;
    }

    ps2_flush_buffer();

    if (ps2_send_device_command(PS2_DEVICE_COMMAND_DISABLE_SCANNING, second_port) != 0xfa) {
        return;
    }
    if (ps2_send_device_command(PS2_DEVICE_COMMAND_IDENTIFY, second_port) != 0xfa) {
        return;
    }

    uint8_t id = ps2_read_data();
    if (id == 0xab) {
        id = ps2_read_data();
        if (id == 0x41 || id == 0x83 || id == 0xc1) {
            klog("[ps2] PS/2 keyboard detected\n");
            ps2_keyboard_init();
        }
    } else if (id == 0x00 || id == 0x03 || id == 0x04) {
        klog("[ps2] PS/2 mouse detected\n");
    }
}

uint8_t ps2_read_data(void) {
    while (!(inb(PS2_STATUS_PORT) & 1)) {
        pause();
    }
    return inb(PS2_DATA_PORT);
}

uint8_t ps2_read_status(void) {
    return inb(PS2_STATUS_PORT);
}

void ps2_send_command(uint8_t command) {
    while (inb(PS2_STATUS_PORT) & 2) {
        pause();
    }
    outb(PS2_COMMAND_PORT, command);
}

void ps2_send_data(uint8_t data) {
    while (inb(PS2_STATUS_PORT) & 2) {
        pause();
    }
    outb(PS2_DATA_PORT, data);
}

void ps2_flush_buffer(void) {
    while (inb(PS2_STATUS_PORT) & 1) {
        inb(PS2_DATA_PORT);
    }
}

uint8_t ps2_send_device_command(uint8_t command, bool second_port) {
    uint8_t res;

    for (int i = 0; i < 3; i++) {
        if (second_port) {
            ps2_send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        ps2_send_data(command);

        res = ps2_read_data();
        if (res != 0xfe) {
            return res;
        }
    }

    return res;
}

uint8_t ps2_send_device_command_with_data(uint8_t command, uint8_t data, bool ack_before_data, bool second_port) {
    uint8_t res;

    for (int i = 0; i < 3; i++) {
        if (second_port) {
            ps2_send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        ps2_send_data(command);

        if (ack_before_data) {
            res = ps2_read_data();

            if (res == 0xfe) {
                continue;
            }
            if (res != 0xfa) {
                return res;
            }
        }

        if (second_port) {
            ps2_send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        ps2_send_data(data);

        res = ps2_read_data();
        if (res != 0xfe) {
            return res;
        }
    }

    return res;
}

__attribute__((section(".unmap_after_init")))
void ps2_init(void) {
    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (fadt != NULL) {
        uint16_t iapc_boot_arch_flags = *(uint16_t*) ((uintptr_t) fadt + 109);
        if (!(iapc_boot_arch_flags & (1 << 1))) {
            klog("[ps2] system lacks a PS/2 controller\n");
            return;
        }
    }

    ps2_send_command(PS2_COMMAND_DISABLE_PORT1);
    ps2_send_command(PS2_COMMAND_DISABLE_PORT2);

    ps2_flush_buffer();

    ps2_send_command(PS2_COMMAND_READ_CONFIG);
    uint8_t config = ps2_read_data();
    config &= ~((1 << 0) | (1 << 1) | (1 << 6));
    ps2_send_command(PS2_COMMAND_WRITE_CONFIG);
    ps2_send_data(config);

    ps2_send_command(PS2_COMMAND_SELF_TEST);
    uint8_t result = ps2_read_data();
    if (result != 0x55) {
        klog("[ps2] PS/2 controller self test failed\n");
        return;
    }

    bool two_channels = false;

    if (config & (1 << 5)) {
        ps2_send_command(PS2_COMMAND_ENABLE_PORT2);

        ps2_send_command(PS2_COMMAND_READ_CONFIG);
        if (!(ps2_read_data() & (1 << 5))) {
            two_channels = true;
        }
    }

    ps2_send_command(PS2_COMMAND_TEST_PORT1);
    bool have_port1 = ps2_read_data() == 0;
    ps2_send_command(PS2_COMMAND_TEST_PORT2);
    bool have_port2 = two_channels && ps2_read_data() == 0;

    if (!have_port1 && !have_port2) {
        klog("[ps2] no usable PS/2 ports detected\n");
        return;
    }

    if (have_port1) {
        ps2_send_command(PS2_COMMAND_ENABLE_PORT1);
    }
    if (have_port2) {
        ps2_send_command(PS2_COMMAND_ENABLE_PORT2);
    }

    ps2_send_command(PS2_COMMAND_READ_CONFIG);
    config = ps2_read_data();

    if (have_port1) {
        config |= (1 << 0);
    }
    if (have_port2) {
        config |= (1 << 1);
    }

    config |= (1 << 6);

    ps2_send_command(PS2_COMMAND_WRITE_CONFIG);
    ps2_send_data(config);

    if (have_port1) {
        port_enum_and_init_device(false);
    }
    if (have_port2) {
        port_enum_and_init_device(true);
    }
}
