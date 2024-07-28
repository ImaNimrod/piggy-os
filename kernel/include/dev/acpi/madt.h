#ifndef _KERNEL_DEV_ACPI_MADT_H
#define _KERNEL_DEV_APCI_MADT_H

#include <cpu/isr.h>
#include <dev/acpi/acpi.h>
#include <dev/lapic.h>
#include <stddef.h>
#include <stdint.h>

#define MADT_LAPIC_ENTRY                0
#define MADT_IOAPIC_ENTRY               1
#define MADT_ISO_ENTRY                  2
#define MADT_LAPIC_NMI_ENTRY            4
#define MADT_LAPIC_ADDR_OVERRIDE_ENTRY  5

struct madt {
    struct acpi_sdt;
    uint32_t lapic_addr;
    uint32_t flags;
    char entries_data[];
} __attribute__((packed));

struct madt_entry_header {
    uint8_t id;
    uint8_t length;
} __attribute__((packed));

struct madt_lapic {
    struct madt_entry_header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct madt_ioapic {
    struct madt_entry_header;
    uint8_t apic_id;
    uint8_t : 8;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    struct madt_entry_header;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct madt_lapic_nmi {
    struct madt_entry_header;
    uint8_t lapic_id;
    uint16_t flags;
    uint8_t lint;
} __attribute__((packed));

struct madt_lapic_address_override {
    struct madt_entry_header;
    uint16_t : 16;
    uint64_t lapic_addr;
} __attribute__((packed));

extern uintptr_t madt_lapic_addr;
extern struct madt_iso* madt_iso_entries[ISA_IRQ_NUM];
extern struct madt_lapic_nmi* madt_lapic_nmi_entries[LAPIC_NMI_NUM];

void madt_init(void);

#endif /* _KERNEL_DEV_ACPI_MADT_H */
