#include <cpu/asm.h>
#include <dev/serial.h>
#include <utils/macros.h>

char serial_getc(uint16_t port) {
    while (!(inb(port + 5) & 0x01)) {
        pause();
    }

    return inb(port + 0);
}

void serial_putc(uint16_t port, char c) {
    while (!(inb(port + 5) & 0x20)) {
        pause();
    }

    outb(port + 0, c);
}

void serial_init(uint16_t port) {
    outb(port + 1, 0x01); // data received IRQ enabled
    outb(port + 3, 0x80);

    outb(port + 0, 0x01); // 115200 baud
    outb(port + 1, 0x00);

    outb(port + 3, 0x03);
    outb(port + 2, 0xc7); // 8-bit data, 1 stop bit, no parity

    outb(port + 4, 0x0f); // enable OUT1, OUT2, and hardware flow control
}
