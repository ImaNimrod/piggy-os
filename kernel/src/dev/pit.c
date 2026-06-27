#include <cpu/asm.h>
#include <dev/pit.h>

#define PIT_CHANNEL2_PORT   0x42
#define PIT_COMMAND_PORT    0x43

void pit_sound_off(void) {
    outb(0x61, inb(0x61) & ~0x03);
}

void pit_sound_on(uint16_t hz) {
    uint16_t div = 1193180 / hz; 

    outb(PIT_COMMAND_PORT, 0xb6);
    outb(PIT_CHANNEL2_PORT, (uint8_t) div);
    outb(PIT_CHANNEL2_PORT, (uint8_t) (div >> 8));

    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 0x03)) {
        outb(0x61, tmp | 0x03);
    }
}
