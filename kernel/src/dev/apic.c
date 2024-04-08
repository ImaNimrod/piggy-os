#include <cpu/asm.h>
#include <dev/apic.h>

void disable_pic(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}
