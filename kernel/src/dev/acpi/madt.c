#include <dev/acpi/madt.h>
#include <dev/apic.h>
#include <dev/ioapic.h>
#include <utils/log.h>

static uintptr_t lapic_addr;

uintptr_t madt_get_lapic_addr(void) {
    return lapic_addr;
}

void madt_init(void) {
    struct madt* madt = (struct madt*) acpi_find_sdt("APIC");
    if (madt == NULL) {
        kpanic(NULL, "system does not have a MADT");
    }

    if (madt->flags & 0x01) {
        disable_pic();
    }

    lapic_addr = (uintptr_t) madt->lapic_addr;

    struct madt_ioapic* ioapic;
    struct madt_iso* iso;
    struct madt_iso* isa_irq_overrides[16] = {0};

    for (uint8_t* entry_ptr = (uint8_t*) madt->entries_data; (uintptr_t) entry_ptr < (uintptr_t) madt + madt->length; entry_ptr += *(entry_ptr + 1)) {
        struct madt_entry_header* entry = (struct madt_entry_header*) entry_ptr;
        switch (entry->id) {
            case MADT_LAPIC_ENTRY: break;
            case MADT_IOAPIC_ENTRY:
                ioapic = (struct madt_ioapic*) entry;
                if (ioapic->gsi_base < 256) {
                    register_ioapic(ioapic->apic_id, (uintptr_t) ioapic->address, ioapic->gsi_base);
                } else { 
                    klog("[acpi] ioapic #%u GSI base out of range\n", ioapic->apic_id);
                }
                break;
            case MADT_ISO_ENTRY:
                iso = (struct madt_iso*) entry;
                if (iso->gsi < 16 && iso->bus_source == 0) {
                    isa_irq_overrides[iso->irq_source] = iso;
                }
                break;
            case MADT_LAPIC_ADDR_OVERRIDE_ENTRY:
                lapic_addr = ((struct madt_lapic_address_override*) entry)->lapic_addr;
                break;
        }
    }

    if (madt->flags & 0x01) {
        for (uint32_t isa_irq = 0; isa_irq < 16; isa_irq++) {
            iso = isa_irq_overrides[isa_irq];
            if (iso == NULL) {
                continue;
            }

            uint8_t dest_irq = iso->gsi;
            if (isa_irq != dest_irq) {
                klog("[acpi] ISA IRQ%u remapped to IRQ%u\n", isa_irq, dest_irq);
            }
        }
    }
}
