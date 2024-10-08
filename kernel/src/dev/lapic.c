#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <dev/acpi/madt.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <mem/vmm.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define NMI_VECTOR 2

// TODO: maybe do x2apic?
#define LAPIC_REG_ID            0x020
#define LAPIC_REG_VER           0x030
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_SVR           0x0f0
#define LAPIC_REG_ESR           0x280
#define LAPIC_REG_CMCI          0x2f0
#define LAPIC_REG_ICR0          0x300
#define LAPIC_REG_ICR1          0x310
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_THERMAL   0x330
#define LAPIC_REG_LVT_PERFCNT   0x340
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURCNT  0x390
#define LAPIC_REG_TIMER_DIV     0x3e0

#define LAPIC_IRQ_MASK          0x10000

static uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*) (madt_lapic_addr + HIGH_VMA + reg));
}

static void lapic_write(uint32_t reg, uint32_t value) {
    *((volatile uint32_t*) (madt_lapic_addr + HIGH_VMA + reg)) = value;
}

UNMAP_AFTER_INIT static void lapic_set_nmi(uint8_t vector, uint32_t current_lapic_id, uint8_t target_lapic_id, uint16_t flags, uint8_t lint) {
    if (target_lapic_id == 0xff) {
        if (current_lapic_id != target_lapic_id) {
            return;
        }
    }

    uint32_t entry = vector | (1 << 10); 

    if (flags & (1 << 1)) {
        entry |= (1 << 13);
    }
    if (flags & (1 << 2)) {
        entry |= (1 << 15);
    }

    if (lint == 0) {
        lapic_write(LAPIC_REG_LVT_LINT0, entry);
    } else if (lint == 1) {
        lapic_write(LAPIC_REG_LVT_LINT1, entry);
    } else {
        kpanic(NULL, false, "invalid LINT number %u for LAPIC NMI", lint);
    }
}

UNMAP_AFTER_INIT static void lapic_timer_calibrate(void) {
    const uint32_t init_ticks = 0xffffffff;

	lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, init_ticks);

    hpet_sleep_ms(10);

    lapic_timer_stop();

	this_cpu()->lapic_frequency = (init_ticks - lapic_read(LAPIC_REG_TIMER_CURCNT)) / 10;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector) {
    lapic_write(LAPIC_REG_ICR1, lapic_id << 24);
    while (lapic_read(LAPIC_REG_ICR0) & (1 << 12));
    lapic_write(LAPIC_REG_ICR0, vector | (1 << 14));
}

void lapic_timer_oneshot(uint8_t vector, uint64_t us) {
    bool prev_int = interrupt_state();
    cli();

    lapic_timer_stop();

    uint64_t ticks = us * (this_cpu()->lapic_frequency / 1000000);
    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, ticks);

    if (prev_int) {
        sti();
    }
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, 0x10000);
}

UNMAP_AFTER_INIT void lapic_init(uint32_t lapic_id) {
    uint64_t lapic_msr = rdmsr(IA32_APIC_BASE_MSR);
    wrmsr(IA32_APIC_BASE_MSR, lapic_msr | (1 << 11));

    if (unlikely(!vmm_map_page(kernel_pagemap, madt_lapic_addr, madt_lapic_addr + HIGH_VMA,
                PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX))) {
        kpanic(NULL, true, "failed to map LAPIC");
    }

    lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) | (1 << 8));

    lapic_timer_calibrate();

    if (((lapic_read(LAPIC_REG_VER) >> 16) & 0xff) >= 4) {
        lapic_write(LAPIC_REG_LVT_PERFCNT, LAPIC_IRQ_MASK);
    }

    struct madt_lapic_nmi* nmi;
    for (size_t i = 0; i < LAPIC_NMI_NUM; i++) {
        nmi = madt_lapic_nmi_entries[i];
        if (nmi != NULL) {
            lapic_set_nmi(NMI_VECTOR, lapic_id, nmi->lapic_id, nmi->flags, nmi->lint);
        }
    }

    lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_IRQ_MASK);
    lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_IRQ_MASK);

    lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_IRQ_MASK);

    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    lapic_eoi();

    lapic_write(LAPIC_REG_TPR, 0);
}

#define PIC1_COMMAND_PORT   0x20
#define PIC1_DATA_PORT      0x21
#define PIC2_COMMAND_PORT   0xa0
#define PIC2_DATA_PORT      0xa1

UNMAP_AFTER_INIT void pic_disable(void) {
    outb(PIC1_COMMAND_PORT, 0x11);
    outb(PIC2_COMMAND_PORT, 0x11);
    outb(PIC1_DATA_PORT, 0x20);
    outb(PIC2_DATA_PORT, 0x28);
    outb(PIC1_DATA_PORT, 0x04);
    outb(PIC2_DATA_PORT, 0x02);
    outb(PIC1_DATA_PORT, 0x01);
    outb(PIC2_DATA_PORT, 0x01);
    outb(PIC1_DATA_PORT, 0xff);
    outb(PIC2_DATA_PORT, 0xff);
}
