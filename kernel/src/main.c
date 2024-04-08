#include <cpu/asm.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/serial.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

void kernel_entry(void) {
    serial_init(COM1);

    gdt_init();
    idt_init();

    pmm_init();
    vmm_init();
    heap_init();

    acpi_init();

    smp_init();

    __asm__ volatile ("sti");
    for (;;) {
        hlt();
    }
}
