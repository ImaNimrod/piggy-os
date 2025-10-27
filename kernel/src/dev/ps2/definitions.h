#ifndef _PS2_DEFINITIONS_H
#define _PS2_DEFINITIONS_H

#include <cpu/asm.h>
#include <stdbool.h>
#include <stdint.h>

#define PS2_KEYBOARD_ISA_IRQ    1
#define PS2_MOUSE_ISA_IRQ       12

#define PS2_DATA_PORT           0x60
#define PS2_COMMAND_PORT        0x64
#define PS2_STATUS_PORT         0x64

#define PS2_COMMAND_READ_CONFIG             0x20
#define PS2_COMMAND_WRITE_CONFIG            0x60
#define PS2_COMMAND_SELF_TEST               0xaa
#define PS2_COMMAND_TEST_PORT1              0xab
#define PS2_COMMAND_TEST_PORT2              0xa9
#define PS2_COMMAND_DISABLE_PORT1           0xad
#define PS2_COMMAND_DISABLE_PORT2           0xa7
#define PS2_COMMAND_ENABLE_PORT1            0xae
#define PS2_COMMAND_ENABLE_PORT2            0xa8
#define PS2_COMMAND_SEND_TO_SECOND_PORT     0xd4

#define PS2_DEVICE_COMMAND_IDENTIFY         0xf2
#define PS2_DEVICE_COMMAND_ENABLE_SCANNING  0xf4
#define PS2_DEVICE_COMMAND_DISABLE_SCANNING 0xf5
#define PS2_DEVICE_COMMAND_RESET            0xff

#define PS2_KEYBOARD_COMMAND_SET_LEDS       0xed

static inline void flush(void) {
    while (inb(PS2_STATUS_PORT) & (1 << 0)) {
        inb(PS2_DATA_PORT);
    }
}

static inline uint8_t read_data(void) {
    while (!(inb(PS2_STATUS_PORT) & (1 << 0))) {
        pause();
    }

    return inb(PS2_DATA_PORT);
}

static inline void send_command(uint8_t command) {
    while (inb(PS2_STATUS_PORT) & (1 << 1)) {
        pause();
    }

    outb(PS2_COMMAND_PORT, command);
}

static inline void send_data(uint8_t data) {
    while (inb(PS2_STATUS_PORT) & (1 << 1)) {
        pause();
    }

    outb(PS2_DATA_PORT, data);
}

void keyboard_init(bool second_port);
void mouse_init(bool second_port);
uint8_t send_device_command(uint8_t command, bool second_port);
uint8_t send_device_command_with_data(uint8_t command, uint8_t data, bool second_port);

#endif /* _PS2_DEFINITIONS_H */
