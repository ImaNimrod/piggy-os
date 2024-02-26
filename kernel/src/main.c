#include <dev/serial.h>
#include <mem/pmm.h>
#include <mem/slab.h>

void kernel_entry(void) {
    serial_init(COM1);

    pmm_init();
    slab_init();

    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}
