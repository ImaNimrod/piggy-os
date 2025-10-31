#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/acpi.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <mem/paging.h>
#include <utils/log.h>
#include <utils/macros.h>

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

#define MADT_IOAPIC_ENTRY       0x01
#define MADT_ISO_ENTRY          0x02
#define MADT_LAPIC_NMI_ENTRY    0x04

#define PIC1_COMMAND_PORT   0x20
#define PIC1_DATA_PORT      0x21
#define PIC2_COMMAND_PORT   0xa0
#define PIC2_DATA_PORT      0xa1

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

static struct madt* madt;

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

static void lapic_setup_nmi(struct madt_lapic_nmi* nmi) {
    if (nmi->lapic_id != this_cpu()->lapic_id && nmi->lapic_id != 0xff) {
        return;
    }

    uint32_t lvt_entry = LAPIC_LVT_DELIVERY_NMI | 2;

    if (nmi->flags & (1 << 1)) {
        lvt_entry |= (1 << 13);
    }
    if (nmi->flags & (1 << 3)) {
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
    madt = (struct madt*) acpi_find_sdt("APIC");
    if (unlikely(madt == NULL)) {
        kpanic(NULL, false, "system does not have a MADT");
    }

    if (likely(madt->flags & (1 << 0))) {
        legacy_pic_disable();
        klog("[lapic] disabled legacy 8259 PIC\n");
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

                ioapic_set_isa_iso(iso->irq_source, iso->gsi, iso->flags);
                break;
        }
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

    for (uint8_t* entry_ptr = (uint8_t*) madt->entries; (uintptr_t) entry_ptr < (uintptr_t) madt + madt->length; entry_ptr += *(entry_ptr + 1)) {
        struct madt_entry_header* entry = (struct madt_entry_header*) entry_ptr;
        if (entry->id == MADT_LAPIC_NMI_ENTRY) {
            lapic_setup_nmi((struct madt_lapic_nmi*) entry);
        }
    }

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    lapic_eoi();
}
