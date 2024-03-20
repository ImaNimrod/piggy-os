#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <dev/serial.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>

void kernel_entry(void) {
    serial_init(COM1);

    gdt_init();
    idt_init();

    pmm_init();
    // slab_init();
    vmm_init();

    smp_init();

    __asm__ volatile ("sti");
    for (;;) __asm__ volatile ("hlt");
}
