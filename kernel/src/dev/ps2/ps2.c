#include <dev/ps2.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/resources.h>
#include <uacpi/utilities.h>

#include "definitions.h"

static const uacpi_char* ps2_keyboard_pnp_ids[] = {
    "PNP0303",
    "PNP0307",
    "PNP030B",
    UACPI_NULL,
};

static const uacpi_char* ps2_mouse_pnp_ids[] = {
    "PNP0F03",
    "PNP0F13",
    UACPI_NULL,
};

enum {
    PS2_DEVICE_KEYBOARD,
    PS2_DEVICE_MOUSE,
};

static int keyboard_irq = 1;
static int mouse_irq = 12;

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

static uacpi_iteration_decision resource_iterator_decision(void* user, uacpi_resource* resource) {
    if (resource == NULL) {
        return UACPI_ITERATION_DECISION_BREAK;
    }

    long device = (long) user;

    if (resource->type == UACPI_RESOURCE_TYPE_IRQ) {
        uacpi_u8 irq = resource->irq.irqs[0];

        if (device == PS2_DEVICE_KEYBOARD) {
            keyboard_irq = irq;
        } else if (device == PS2_DEVICE_MOUSE) {
            mouse_irq = irq;
        }

        return UACPI_ITERATION_DECISION_BREAK;
    }

    return UACPI_ITERATION_DECISION_NEXT_PEER;
}

static uacpi_iteration_decision match_ps2_device(void* user, uacpi_namespace_node* node, uacpi_u32) {
    uacpi_resources* resources;

    uacpi_status ret = uacpi_get_current_resources(node, &resources);
    if (uacpi_unlikely_error(ret)) {
        klog("[ps2] unable to retrieve PS/2 device ACPI resources: %s\n", uacpi_status_to_string(ret));
        return UACPI_ITERATION_DECISION_NEXT_PEER;
    }

    uacpi_for_each_resource(resources, resource_iterator_decision, user);

    uacpi_free_resources(resources);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

void ps2_init(void) {
    bool brokenps2 = cmdline_get("brokenps2") != NULL;
    if (brokenps2) {
        keyboard_irq = PS2_KEYBOARD_ISA_IRQ;
        mouse_irq = PS2_MOUSE_ISA_IRQ;
    } else {
        uacpi_find_devices_at(uacpi_namespace_root(), ps2_keyboard_pnp_ids, match_ps2_device, (void*) PS2_DEVICE_KEYBOARD);
        uacpi_find_devices_at(uacpi_namespace_root(), ps2_mouse_pnp_ids, match_ps2_device, (void*) PS2_DEVICE_MOUSE);
    }

    send_command(PS2_COMMAND_DISABLE_PORT1);
    send_command(PS2_COMMAND_DISABLE_PORT2);

    flush();

    if (keyboard_irq != -1) {
        send_command(PS2_COMMAND_ENABLE_PORT1);

        send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, false);
        keyboard_init((uint8_t) keyboard_irq);
    }

    if (mouse_irq != -1) {
        send_command(PS2_COMMAND_ENABLE_PORT2);

        send_device_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, true);
        mouse_init((uint8_t) mouse_irq);
    }
}
