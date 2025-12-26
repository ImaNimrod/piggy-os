#include <dev/ps2.h>
#include <stddef.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#include "definitions.h"

static void port_enum_and_init_device(bool second_port) {
    if (send_device_command(PS2_DEVICE_COMMAND_RESET, second_port) != 0xfa) {
        return;
    }
    if (read_data() != 0xaa) {
        return;
    }

    flush();

    if (send_device_command(PS2_DEVICE_COMMAND_DISABLE_SCANNING, second_port) != 0xfa) {
        return;
    }

    if (send_device_command(PS2_DEVICE_COMMAND_IDENTIFY, second_port) != 0xfa) {
        return;
    }

    uint8_t id = read_data();
    if (id == 0xab) {
        id = read_data();
        if (id == 0x41 || id == 0x83 || id == 0xc1) {
            klog("[ps2] PS/2 keyboard detected\n");
            keyboard_init(second_port);
        }
    } else if (id == 0x00 || id == 0x03 || id == 0x04) {
        klog("[ps2] PS/2 mouse detected\n");
        mouse_init(second_port);
    }

    send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, second_port);
}

uint8_t send_device_command(uint8_t command, bool second_port) {
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

uint8_t send_device_command_with_data(uint8_t command, uint8_t data, bool second_port) {
    uint8_t res;

    for (int i = 0; i < 3; i++) {
        if (second_port) {
            send_command(PS2_COMMAND_SEND_TO_SECOND_PORT);
        }

        send_data(command);

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

void ps2_init(void) {
    bool brokenps2 = cmdline_get("brokenps2") != NULL;
    if (brokenps2) {
        klog("[ps2] 'brokenps2' argument found, assuming broken PS/2 controller emulation\n");

        send_command(PS2_COMMAND_DISABLE_PORT1);
        send_command(PS2_COMMAND_DISABLE_PORT2);

        flush();

        send_command(PS2_COMMAND_ENABLE_PORT1);

        keyboard_init(false);
        return;
    }

    struct uacpi_table fadt_table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_FADT_SIGNATURE, &fadt_table);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "unable to find FADT table: %s", uacpi_status_to_string(ret));
    }

    struct acpi_fadt* fadt = fadt_table.ptr;
    uint16_t iapc_boot_arch = fadt->iapc_boot_arch;
    uacpi_table_unref(&fadt_table);

    if (!(iapc_boot_arch & ACPI_IA_PC_8042)) {
        klog("[ps2] system missing 8042 PS/2 controller\n");
        return;
    }

    send_command(PS2_COMMAND_DISABLE_PORT1);
    send_command(PS2_COMMAND_DISABLE_PORT2);

    flush();

    send_command(PS2_COMMAND_READ_CONFIG);
    uint8_t config = read_data();
    config &= ~((1 << 0) | (1 << 4) | (1 << 6));
    send_command(PS2_COMMAND_WRITE_CONFIG);
    send_data(config);

    send_command(PS2_COMMAND_SELF_TEST);
    uint8_t result = read_data();
    if (result != 0x55) {
        klog("[ps2] PS/2 controller self test failed\n");
        return;
    }

    send_command(PS2_COMMAND_WRITE_CONFIG);
    send_data(config);

    bool dual_port = false;

    if (config & (1 << 5)) {
        send_command(PS2_COMMAND_ENABLE_PORT2);

        send_command(PS2_COMMAND_READ_CONFIG);
        if (!(read_data() & (1 << 5))) {
            send_command(PS2_COMMAND_DISABLE_PORT2);

            config &= ~((1 << 1) | (1 << 5));
            send_command(PS2_COMMAND_WRITE_CONFIG);
            send_data(config);

            dual_port = true;
        }
    }

    send_command(PS2_COMMAND_TEST_PORT1);
    bool have_port1 = read_data() == 0;
    send_command(PS2_COMMAND_TEST_PORT2);
    bool have_port2 = dual_port && (read_data() == 0);

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

    if (have_port1) {
        port_enum_and_init_device(false);
    }
    if (have_port2) {
        port_enum_and_init_device(true);
    }
}
