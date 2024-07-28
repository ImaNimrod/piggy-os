#ifndef _KERNEL_DEV_PS2_H
#define _KERNEL_DEV_PS2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KBDDEV_MAJ 5

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

uint8_t ps2_read_data(void);
void ps2_send_command(uint8_t command);
void ps2_send_data(uint8_t data);
void ps2_flush_buffer(void);
uint8_t ps2_send_device_command(uint8_t command, bool second_port);
uint8_t ps2_send_device_command_with_data(uint8_t command, uint8_t data, bool ack_before_data, bool second_port);
void ps2_keyboard_init(void);
void ps2_init(void);

#endif /* _KERNEL_DEV_PS2_H */
