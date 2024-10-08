#ifndef _KERNEL_DEV_SERIAL_H
#define _KERNEL_DEV_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

#define COM1 0x3f8
#define COM2 0x2f8
#define COM3 0x3e8
#define COM4 0x2e8

char serial_getc(uint16_t port);
void serial_putc(uint16_t port, char c);
bool serial_init(uint16_t port); 

#endif /* _KERNEL_DEV_SERIAL_H */
