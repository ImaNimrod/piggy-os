#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/lapic.h>
#include <dev/pci.h>
#include <mem/vmm.h>
#include <utils/log.h>

__attribute__((noreturn)) static void legacy_8042_reset(void) {
    uint8_t val;
    do {
        val = inb(0x64);
        if (val & (1 << 0)) {
            inb(0x60);
        }
    } while (val & (1 << 1));

    outb(0x64, 0xfe);

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

__attribute__((noreturn)) void acpi_reboot(void) {
    cli();

    size_t cpu_number = this_cpu()->cpu_number;
    for (size_t i = 0; i < smp_cpu_count; i++) {
        if (i != cpu_number) {
            lapic_send_ipi(percpus[i].lapic_id, PANIC_IPI);
        }
    }

    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (fadt == NULL) {
        legacy_8042_reset();
    }

    uint32_t flags = *(uint32_t*) ((uintptr_t) fadt + 112);

    if (!use_acpi_rev2 || !(flags & (1 << 10))) {
        legacy_8042_reset();
    }

    uint8_t reset_value = *(uint8_t*) ((uintptr_t) fadt + 128);
    struct acpi_gas* reset_register = (struct acpi_gas*) ((uintptr_t) fadt + 116);

    switch (reset_register->address_space) {
        case GAS_ADDRESS_SPACE_MEMORY:
            vmm_map_page(kernel_pagemap, reset_register->base, reset_register->base, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
            *(uint8_t*) ((uintptr_t) reset_register->base) = reset_value;
            break;
        case GAS_ADDRESS_SPACE_IO:
            outb(reset_register->base, reset_value);
            break;
        case GAS_ADDRESS_SPACE_PCI:
            pci_write(0,                                    /* bus */
                    (reset_register->base >> 32) & 0xff,    /* slot */
                    (reset_register->base >> 16) & 0xff,    /* function */
                    reset_register->base & 0xffff,          /* offset */
                    reset_value,
                    1);
            break;
        default:
            legacy_8042_reset();
            break;
    }

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

__attribute__((noreturn)) void acpi_shutdown(void) {
    cli();

    size_t cpu_number = this_cpu()->cpu_number;
    for (size_t i = 0; i < smp_cpu_count; i++) {
        if (i != cpu_number) {
            lapic_send_ipi(percpus[i].lapic_id, PANIC_IPI);
        }
    }

    /*
     * check if system is running under a hypervisor and attempt to use a variety of
     * emulator-specific command sequences to initiate system shutdown
     */
    uint32_t ecx, unused;
    if (cpuid(1, 0, &unused, &unused, &ecx, &unused) && ecx & (1 << 31)) {
        outw(0x604, 0x2000);    // QEMU
        outw(0xb004, 0x2000);   // BOCHS and QEMU < 2.0
        outw(0x4004, 0x3400);   // VirtualBox
    } else {
        // TODO: implement ACPI shutdown
    }

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}
