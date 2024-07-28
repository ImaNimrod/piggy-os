#include <dev/acpi/madt.h>
#include <dev/ioapic.h>
#include <utils/log.h>

uintptr_t madt_lapic_addr;
struct madt_iso* madt_iso_entries[ISA_IRQ_NUM];
struct madt_lapic_nmi* madt_lapic_nmi_entries[LAPIC_NMI_NUM];

void madt_init(void) {
    struct madt* madt = (struct madt*) acpi_find_sdt("APIC");
    if (madt == NULL) {
        kpanic(NULL, "system does not have a MADT");
    }

    if (madt->flags & 1) {
        disable_pic();
    }

    madt_lapic_addr = (uintptr_t) madt->lapic_addr;

    struct madt_ioapic* ioapic;
    struct madt_iso* iso;
    struct madt_lapic_nmi* lapic_nmi;

    for (uint8_t* entry_ptr = (uint8_t*) madt->entries_data; (uintptr_t) entry_ptr < (uintptr_t) madt + madt->length; entry_ptr += *(entry_ptr + 1)) {
        struct madt_entry_header* entry = (struct madt_entry_header*) entry_ptr;
        switch (entry->id) {
            case MADT_LAPIC_ENTRY: break;
            case MADT_IOAPIC_ENTRY:
                ioapic = (struct madt_ioapic*) entry;
                if (ioapic->gsi_base < ISR_HANDLER_NUM) {
                    register_ioapic(ioapic->apic_id, (uintptr_t) ioapic->address, ioapic->gsi_base);
                } else { 
                    klog("[acpi] ioapic #%u GSI base out of range\n", ioapic->apic_id);
                }
                break;
            case MADT_ISO_ENTRY:
                iso = (struct madt_iso*) entry;
                if (iso->gsi < ISA_IRQ_NUM && iso->bus_source == 0) {
                    madt_iso_entries[iso->irq_source] = iso;
                }
                break;
            case MADT_LAPIC_NMI_ENTRY:
                lapic_nmi = (struct madt_lapic_nmi*) entry;
                madt_lapic_nmi_entries[lapic_nmi->lint] = lapic_nmi;
                break;
            case MADT_LAPIC_ADDR_OVERRIDE_ENTRY:
                madt_lapic_addr = ((struct madt_lapic_address_override*) entry)->lapic_addr;
                break;
        }
    }
}
