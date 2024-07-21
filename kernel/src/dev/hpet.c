#include <cpu/asm.h>
#include <dev/acpi/acpi.h>
#include <dev/hpet.h>
#include <mem/vmm.h>
#include <utils/log.h>

#define HPET_VADDR 0xfffffffffffff000

#define HPET_ID                 0x000
#define HPET_CONFIG             0x010
#define HPET_STATUS             0x020
#define HPET_COUNT              0x0f0
#define HPET_TIMER_CONFIG_BASE  0x100
#define HPET_TIMER_VALUE_BASE   0x108
#define HPET_TIMER_FSB_IRR_BASE 0x110

struct hpet_table {
    struct acpi_sdt;
    uint8_t hardware_rev_id;
    uint8_t comparator_count: 5;
    uint8_t counter_size: 1;
    uint8_t reserved: 1;
    uint8_t legacy_replacement: 1;
    uint16_t pci_vendor_id;
    struct acpi_gas address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

uint32_t hpet_clock_period = 0;

static inline uint64_t hpet_read(uint32_t reg) {
    return *((volatile uint64_t*) (HPET_VADDR + reg));
}

static inline void hpet_write(uint32_t reg, uint64_t value) {
    *((volatile uint64_t*) (HPET_VADDR + reg)) = value;
}

uint64_t hpet_count(void) {
    return hpet_read(HPET_COUNT);
}

void hpet_sleep_ms(uint64_t ms) {
    uint64_t target = hpet_read(HPET_COUNT) + (ms * 1000000000000) / hpet_clock_period;
    while (hpet_read(HPET_COUNT) < target) {
        pause();
    };
}

void hpet_sleep_ns(uint64_t ns) {
    uint64_t target = hpet_read(HPET_COUNT) + (ns * 1000000) / hpet_clock_period;
    while (hpet_read(HPET_COUNT) < target) {
        pause();
    }
}

void hpet_init(void) {
    struct hpet_table* hpet_table = (struct hpet_table*) acpi_find_sdt("HPET");
    if (hpet_table == NULL) {
        kpanic(NULL, "system does not have a HPET");
    }

    uintptr_t hpet_paddr = hpet_table->address.base;

    vmm_map_page(kernel_pagemap, HPET_VADDR, hpet_paddr,
            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX);

    hpet_clock_period = hpet_read(HPET_ID) >> 32;
    hpet_write(HPET_CONFIG, 0);
    hpet_write(HPET_COUNT, 0);
    hpet_write(HPET_CONFIG, 1);

    klog("[hpet] hpet (address=0x%x) initialized\n", hpet_paddr);
}
