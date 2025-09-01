#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/acpi.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <mem/paging.h>
#include <stddef.h>
#include <sys/timer.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define HPET_REG_ID     0x000
#define HPET_REG_CONFIG 0x010
#define HPET_REG_ISR    0x020
#define HPET_REG_COUNT  0x0f0
#define HPET_REG_TIMER_CONFIG(n) (0x100 + 0x20 * n)
#define HPET_REG_TIMER_COUNT(n) (0x108 + 0x20 * n)
#define HPET_REG_TIMER_COUNT(n) (0x108 + 0x20 * n)

#define PIT_ISA_IRQ 0

struct hpet_table {
    struct acpi_sdt;
    uint8_t hardware_rev_id;
    uint8_t comparator_count: 5;
    uint8_t counter_size: 1;
    uint8_t : 1;
    uint8_t legacy_replacement: 1;
    uint16_t pci_vendor_id;
    struct acpi_gas address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

static uintptr_t hpet_addr = 0;
static uint32_t clock_period_ns = 0;

static inline uint64_t hpet_read(uint32_t reg) {
    return mmio_read64((void*) (hpet_addr + reg));
}

static inline void hpet_write(uint32_t reg, uint64_t value) {
    mmio_write64((void*) (hpet_addr + reg), value);
}

static void hpet_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;
    timer_update_timers();
}

void hpet_sleep_ns(uint64_t ns) {
    uint64_t start = hpet_read(HPET_REG_COUNT);
    uint64_t delta = ns / clock_period_ns;
    while (hpet_read(HPET_REG_COUNT) - start < delta) {
        pause();
    }
}

void hpet_init(uint32_t hz) {
    struct hpet_table* hpet_table = (struct hpet_table*) acpi_find_sdt("HPET");
    if (unlikely(hpet_table == NULL)) {
        kpanic(NULL, false, "system does not have an HPET\n");
    }

    uintptr_t hpet_paddr = hpet_table->address.base;
    hpet_addr = hpet_paddr + HIGH_VMA;

    pagemap_map(kernel_pagemap, hpet_addr, hpet_paddr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);

    /* if HPET doesnt support legacy IRQ replacement, we can't use it */
    if (!(hpet_read(HPET_REG_ID) & (1 << 15))) {
        kpanic(NULL, false, "HPET does not support legacy IRQ replacement mode\n");
    }

    hpet_write(HPET_REG_CONFIG, 0);
    hpet_write(HPET_REG_COUNT, 0);

    uint32_t clock_period_fs = hpet_read(HPET_REG_ID) >> 32;
    clock_period_ns = clock_period_fs / 1000000;

    uint32_t ns_per_irq = 1000000000 / hz;
    uint64_t ticks_per_irq = ns_per_irq / clock_period_ns;

    hpet_write(HPET_REG_TIMER_COUNT(0), ticks_per_irq);
    hpet_write(HPET_REG_TIMER_CONFIG(0), (1 << 2) | (1 << 3) | (1 << 6));

    isr_register_handler(PIT_ISA_IRQ + ISA_IRQ_BASE, hpet_irq_handler, NULL);
    ioapic_redirect_irq(PIT_ISA_IRQ, PIT_ISA_IRQ + ISA_IRQ_BASE);
    ioapic_set_irq_mask(PIT_ISA_IRQ, false);

    /* renable HPET in legacy replacement mode */
    hpet_write(HPET_REG_CONFIG, (1 << 0) | (1 << 1));

    klog("[hpet] initialized HPET (address: 0x%lx, period: %uns)\n", hpet_paddr, clock_period_ns);
}
