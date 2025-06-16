#include <cpu/isr.h>
#include <dev/acpi.h>
#include <dev/ioapic.h>
#include <dev/lapic.h>
#include <stddef.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

// TODO: handle non maskable interrupts properly

#define MADT_IOAPIC_ENTRY               0x01
#define MADT_ISO_ENTRY                  0x02
#define MADT_LAPIC_NMI_ENTRY            0x04

struct madt {
    struct acpi_sdt;
    uint32_t lapic_addr;
    uint32_t flags;
    char entries[];
} __attribute__((packed));

struct madt_entry_header {
    uint8_t id;
    uint8_t length;
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

void madt_parse(void) {
    struct madt* madt = (struct madt*) acpi_find_sdt("APIC");
    if (unlikely(madt == NULL)) {
        kpanic(NULL, false, "system does not have a MADT");
    }

    if (likely(madt->flags & (1 << 0))) {
        legacy_pic_disable();
        klog("[acpi] disabled legacy 8259 PIC\n");
    }

    struct madt_ioapic* ioapic;
    struct madt_iso* iso;

    for (uint8_t* entry_ptr = (uint8_t*) madt->entries; (uintptr_t) entry_ptr < (uintptr_t) madt + madt->length; entry_ptr += *(entry_ptr + 1)) {
        struct madt_entry_header* entry = (struct madt_entry_header*) entry_ptr;
        switch (entry->id) {
            case MADT_IOAPIC_ENTRY:
                ioapic = (struct madt_ioapic*) entry;
                ioapic_init(ioapic->apic_id, (uintptr_t) ioapic->address, ioapic->gsi_base);
                break;
            case MADT_ISO_ENTRY:
                iso = (struct madt_iso*) entry;

                if (iso->bus_source != 0) {
                    break;
                }

                int polarity;
                int trigger_mode;

                uint8_t polarity_flags = iso->flags & 0x03;
                if (polarity_flags == 0x00 || polarity_flags == 0x03) {
                    polarity = IOAPIC_POLARITY_ACTIVE_LOW;
                } else if (polarity_flags == 0x01) {
                    polarity = IOAPIC_POLARITY_ACTIVE_HIGH;
                } else {
                    kpanic(NULL, false, "invalid IRQ polarity flags on APCI interrupt source override");
                }

                uint8_t trigger_flags = (iso->flags >> 2) & 0x03;
                if (trigger_flags == 0x00 || trigger_flags == 0x01) {
                    trigger_mode = IOAPIC_TRIGGER_EDGE;
                } else if (trigger_flags == 0x03) {
                    trigger_mode = IOAPIC_TRIGGER_LEVEL;
                } else {
                    kpanic(NULL, false, "invalid IRQ trigger flags on APCI interrupt source override");
                }

                ioapic_set_isa_iso(iso->irq_source, iso->gsi, polarity, trigger_mode);
                break;
        }
    }
}
