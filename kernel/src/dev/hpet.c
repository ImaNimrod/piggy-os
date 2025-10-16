#include <cpu/asm.h>
#include <dev/acpi.h>
#include <dev/hpet.h>
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

static uintptr_t hpet_addr;
static uint32_t clock_period_ns;

static inline uint64_t hpet_read(uint32_t reg) {
    return mmio_read64((void*) (hpet_addr + reg));
}

static inline void hpet_write(uint32_t reg, uint64_t value) {
    mmio_write64((void*) (hpet_addr + reg), value);
}

void hpet_sleep_ns(uint64_t ns) {
    uint64_t start = hpet_read(HPET_REG_COUNT);
    uint64_t delta = ns / clock_period_ns;
    while (hpet_read(HPET_REG_COUNT) - start < delta) {
        pause();
    }
}

void hpet_init(void) {
    struct hpet_table* hpet_table = (struct hpet_table*) acpi_find_sdt("HPET");
    if (unlikely(hpet_table == NULL)) {
        kpanic(NULL, false, "system does not have an HPET\n");
    }

    uintptr_t hpet_paddr = hpet_table->address.base;
    hpet_addr = hpet_paddr + HIGH_VMA;

    pagemap_map(kernel_pagemap, hpet_addr, hpet_paddr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);

    clock_period_ns = (hpet_read(HPET_REG_ID) >> 32) / 1000000;
    hpet_write(HPET_REG_CONFIG, 0);
    hpet_write(HPET_REG_COUNT, 0);
    hpet_write(HPET_REG_CONFIG, (1 << 0));

    klog("[hpet] initialized HPET (address: 0x%lx, period: %uns)\n", hpet_paddr, clock_period_ns);
}
