#ifndef _KERNEL_DEV_SERIAL_H
#define _KERNEL_DEV_SERIAL_H 1

#include <stdint.h>

#define PORT_COM1 0x03f8
#define PORT_COM2 0x02f8
#define PORT_COM3 0x03e8
#define PORT_COM4 0x02e8

char serial_getc(uint16_t port);
void serial_putc(uint16_t port, char c);
void serial_init(uint16_t port);

#endif /* _KERNEL_DEV_SERIAL_H */
