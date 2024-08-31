#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <dev/acpi/madt.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <mem/vmm.h>
#include <utils/panic.h>

#define LAPIC_VADDR 0xffffffffffffe000
#define NMI_VECTOR 2

// TODO: maybe do x2apic?
#define LAPIC_REG_ID            0x020
#define LAPIC_REG_VER           0x030
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_SVR           0x0f0
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

static uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*) (LAPIC_VADDR + reg));
}

static void lapic_write(uint32_t reg, uint32_t value) {
    *((volatile uint32_t*) (LAPIC_VADDR + reg)) = value;
}

static void lapic_set_nmi(uint8_t vector, uint32_t current_lapic_id, uint8_t target_lapic_id, uint16_t flags, uint8_t lint) {
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

static void lapic_timer_calibrate(void) {
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

void lapic_init(uint32_t lapic_id) {
    uint64_t lapic_msr = rdmsr(IA32_APIC_BASE_MSR);
    wrmsr(IA32_APIC_BASE_MSR, lapic_msr | (1 << 11));

    if (!vmm_map_page(kernel_pagemap, LAPIC_VADDR, madt_lapic_addr,
            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX)) {
        kpanic(NULL, true, "failed to map LAPIC");
    }

    lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) | (1 << 8));

    struct madt_lapic_nmi* nmi;
    for (size_t i = 0; i < LAPIC_NMI_NUM; i++) {
        nmi = madt_lapic_nmi_entries[i];
        if (nmi != NULL) {
            lapic_set_nmi(NMI_VECTOR, lapic_id, nmi->lapic_id, nmi->flags, nmi->lint);
        }
    }

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_TPR, 0);
}

void disable_pic(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}
