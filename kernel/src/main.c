#include <cpu/asm.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/serial.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

void kernel_entry(void) {
    cli();

    serial_init(COM1);

    pmm_init();
    vmm_init();
    heap_init();

    acpi_init();

    smp_init();

    sti();
    for (;;) {
        hlt();
    }
}
