#include <cpu/asm.h>
#include <cpu/ioapic.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <dev/hpet.h>
#include <mem/paging.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
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
    mfence();
}

static void lapic_timer_calibrate(void) {
    lapic_write(LAPIC_REG_TIMER_DIV, 3);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0xffffffff);

    timer_wait_ns(MS_TO_NS(1));

    uint32_t count = lapic_timer_stop();

    this_cpu()->lapic_ticks_per_ms = 0xffffffff - count;
}

static uacpi_iteration_decision parse_madt(void* user, struct acpi_entry_hdr* entry) {
    (void) user;

    if (entry->type == ACPI_MADT_ENTRY_TYPE_LAPIC_NMI) {
        struct acpi_madt_lapic_nmi* nmi_entry = (struct acpi_madt_lapic_nmi*) entry;

        if (nmi_entry->uid == this_cpu()->lapic_id || nmi_entry->uid == 0xff) {
            uint32_t lvt_entry = 2 | LAPIC_LVT_DELIVERY_NMI;

            uint8_t polarity_flags = nmi_entry->flags & ACPI_MADT_POLARITY_MASK;
            if (polarity_flags == ACPI_MADT_POLARITY_CONFORMING || polarity_flags == ACPI_MADT_POLARITY_ACTIVE_LOW) {
                lvt_entry |= (1 << 13);
            }

            uint8_t trigger_mode_flags = nmi_entry->flags & ACPI_MADT_TRIGGERING_MASK;
            if (trigger_mode_flags == ACPI_MADT_TRIGGERING_LEVEL) {
                lvt_entry |= (1 << 15);
            }

            lapic_write(LAPIC_REG_LVT_LINT0 + (nmi_entry->lint * 0x10), lvt_entry);
        }
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector) {
    // Wait for previous IPI to finish delivery
    if (!use_x2apic) {
        while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12)) {
            pause();
        }
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
            icr_high = use_x2apic ? lapic_id : (lapic_id << 24);
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

uint32_t lapic_timer_stop(void) {
    uint32_t count = lapic_read(LAPIC_REG_TIMER_CURCNT);

    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASK);

    return count;
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

    struct uacpi_table table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "unable to find MADT table: %s", uacpi_status_to_string(ret));
    }

    uacpi_for_each_subtable(table.hdr, sizeof(struct acpi_madt), parse_madt, NULL);
    uacpi_table_unref(&table);

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    lapic_eoi();
}
