#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <mem/paging.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#define LAPIC_REG_ID            0x020
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_LDR           0x0d0
#define LAPIC_REG_DFR           0x0e0
#define LAPIC_REG_SIVR          0x0f0
#define LAPIC_REG_ESR           0x280
#define LAPIC_REG_ICR_LOW       0x300
#define LAPIC_REG_ICR_HIGH      0x310
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_THERMAL   0x330
#define LAPIC_REG_LVT_PERFCNT   0x340
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURCNT  0x390
#define LAPIC_REG_TIMER_DIV     0x3e0

#define LAPIC_LVT_DELIVERY_NMI  (4 << 8)
#define LAPIC_LVT_MASK          (1 << 16)

#define PIC1_COMMAND_PORT       0x20
#define PIC1_DATA_PORT          0x21
#define PIC2_COMMAND_PORT       0xa0
#define PIC2_DATA_PORT          0xa1

static struct acpi_madt* madt;

static inline uint32_t lapic_read(uint32_t reg) {
    if (use_x2apic) {
        return rdmsr(0x800 + (reg >> 4));
    }

    return mmio_read32((void*) (bsp_lapic_addr + HIGH_VMA + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    if (use_x2apic) {
        wrmsr(0x800 + (reg >> 4), value);
        return;
    }

    mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + reg), value);
}

static void lapic_setup_nmi(struct acpi_madt_lapic_nmi* nmi) {
    if (nmi->uid != this_cpu()->lapic_id && nmi->uid != 0xff) {
        return;
    }

    uint32_t lvt_entry = LAPIC_LVT_DELIVERY_NMI | 2;

    uint8_t polarity_flags = nmi->flags & ACPI_MADT_POLARITY_MASK;
    if (polarity_flags == ACPI_MADT_POLARITY_CONFORMING || polarity_flags == ACPI_MADT_POLARITY_ACTIVE_LOW) {
        lvt_entry |= (1 << 13);
    }

    uint8_t trigger_mode_flags = nmi->flags & ACPI_MADT_TRIGGERING_MASK;
    if (trigger_mode_flags == ACPI_MADT_TRIGGERING_LEVEL) {
        lvt_entry |= (1 << 15);
    }

    lapic_write(LAPIC_REG_LVT_LINT0 + (nmi->lint * 0x10), lvt_entry);
}

static void lapic_timer_calibrate(void) {
    lapic_write(LAPIC_REG_TIMER_DIV, 3);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0xffffffff);

    hpet_sleep_ns(MS_TO_NS(1));

    uint32_t count = lapic_read(LAPIC_REG_TIMER_CURCNT);
    lapic_timer_stop();

    this_cpu()->lapic_ticks_per_ms = 0xffffffff - count;
}

static void legacy_pic_disable(void) {
    /* mask all PIC interrupts, */
    outb(PIC1_DATA_PORT, 0xff);
    outb(PIC2_DATA_PORT, 0xff);

    /* then remap PIC interrupts to 0x20 - 0x30 to avoid conflicts with builtin CPU exceptions */
    outb(PIC1_COMMAND_PORT, 0x11);
    outb(PIC2_COMMAND_PORT, 0x11);
    outb(PIC1_DATA_PORT, 0x20);
    outb(PIC2_DATA_PORT, 0x28);
    outb(PIC1_DATA_PORT, 0x04);
    outb(PIC2_DATA_PORT, 0x02);
    outb(PIC1_DATA_PORT, 0x01);
    outb(PIC2_DATA_PORT, 0x01);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector) {
    /* wait for previous IPI to finish delivery */
    while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12)) {
        pause();
    }

    uint32_t icr_low = vector | (1 << 14);
    uint32_t icr_high = 0;

    switch (lapic_id) {
        case LAPIC_IPI_SELF:
            icr_low |= (0x01 << 18);
            break;
        case LAPIC_IPI_ALL_CPUS:
            icr_low |= (0x02 << 18);
            break;
        case LAPIC_IPI_ALL_OTHER_CPUS:
            icr_low |= (0x03 << 18);
            break;
        default:
            icr_high |= lapic_id;
            break;
    }

    if (use_x2apic) {
        wrmsr(0x830, ((uint64_t) icr_high << 32) | icr_low);
    } else {
        mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + LAPIC_REG_ICR_HIGH), icr_high);
        mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + LAPIC_REG_ICR_LOW), icr_low);
    }
}

void lapic_timer_oneshot(uint8_t vector, uint64_t ms) {
    lapic_timer_stop();
    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_DIV, 3);
    lapic_write(LAPIC_REG_TIMER_INITCNT, ms * this_cpu()->lapic_ticks_per_ms);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASK);
}

void lapic_madt_parse(void) {
    struct uacpi_table madt_table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &madt_table);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "unable to find MADT table: %s", uacpi_status_to_string(ret));
    }

    madt = madt_table.ptr;
    if (likely(madt->flags & ACPI_PCAT_COMPAT)) {
        legacy_pic_disable();
        klog("[lapic] disabled legacy 8259 PIC\n");
    }

    struct acpi_madt_ioapic* ioapic;
    struct acpi_madt_interrupt_source_override* iso;

    uint8_t* current_ptr = (uint8_t*) madt->entries;
    uint8_t* end_ptr = (uint8_t*) madt->entries + madt->hdr.length;
    while (current_ptr < end_ptr) {
        struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*) current_ptr;
        switch (entry->type) {
            case ACPI_MADT_ENTRY_TYPE_IOAPIC:
                ioapic = (struct acpi_madt_ioapic*) entry;
                ioapic_init(ioapic->id, ioapic->address, ioapic->gsi_base);
                break;
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE:
                iso = (struct acpi_madt_interrupt_source_override*) entry;
                if (iso->bus != 0) {
                    break;
                }
                ioapic_set_isa_iso(iso->source, iso->gsi, iso->flags);
                break;
        }

        current_ptr += entry->length;
    }
}

void lapic_percpu_init(void) {
    lapic_write(LAPIC_REG_SIVR, lapic_read(LAPIC_REG_SIVR) | (1 << 8));

    if (!use_x2apic) {
        lapic_write(LAPIC_REG_DFR, 0xf0000000);
        lapic_write(LAPIC_REG_LDR, lapic_read(LAPIC_REG_ID));
    }

    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_THERMAL, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_PERFCNT, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASK);

    uint8_t* current_ptr = (uint8_t*) madt->entries;
    uint8_t* end_ptr = (uint8_t*) madt->entries + madt->hdr.length;
    while (current_ptr < end_ptr) {
        struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*) current_ptr;
        if (entry->type == ACPI_MADT_ENTRY_TYPE_LAPIC_NMI) {
            lapic_setup_nmi((struct acpi_madt_lapic_nmi*) entry);
        }

        current_ptr += entry->length;
    }

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    lapic_eoi();
}
