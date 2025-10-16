#ifndef _KERNEL_DEV_SERIAL_H
#define _KERNEL_DEV_SERIAL_H

#include <stdint.h>

#define COM1_PORT 0x03f8
#define COM2_PORT 0x02f8
#define COM3_PORT 0x03e8
#define COM4_PORT 0x02e8

char serial_getc(uint16_t port);
void serial_putc(uint16_t port, char c);
void serial_init(uint16_t port);

#endif /* _KERNEL_DEV_SERIAL_H */
