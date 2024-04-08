#include <cpu/asm.h>
#include <dev/serial.h>

int serial_init(uint16_t port) {
    outb(port + 7, 0xaa);
    if (inb(port + 7) != 0xaa) {
        return 1;
    }

    outb(port + 1, 0x01);
    outb(port + 3, 0x80);

    outb(port + 0, 0x03);
    outb(port + 1, 0x00);

    outb(port + 3, 0x03);
    outb(port + 2, 0xc7);
    outb(port + 4, 0x0b);

    return 0;
}

char serial_getc(uint16_t port) {
    while (!(inb(port + 5) & 0x01)) {
        pause();
    }

    return inb(port);
}

void serial_putc(uint16_t port, char c) {
    while (inb(port + 5) & 0x80) {
        pause();
    }

    outb(port, c);
}

void serial_puts(uint16_t port, const char* s) {
    while (*s) {
        serial_putc(port, *s);
        s++;
    }
}
