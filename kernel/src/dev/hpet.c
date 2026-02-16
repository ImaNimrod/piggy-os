#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <mem/paging.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#define HPET_REG_ID                 0x000
#define HPET_REG_CONFIG             0x010
#define HPET_REG_ISR                0x020
#define HPET_REG_COUNT              0x0f0
#define HPET_REG_TIMER_CONFIG(n)    (0x100 + 0x20 * n)
#define HPET_REG_TIMER_COUNT(n)     (0x108 + 0x20 * n)

#define HPET_ENABLE_CNF (1 << 0)

#define TSC_CALIBRATION_TIME_MS 2

static struct timer_info hpet_timer_info;

static inline uint64_t hpet_read(uintptr_t base, uint32_t reg) {
    return mmio_read64((void*) (base + reg));
}

static inline void hpet_write(uintptr_t base, uint32_t reg, uint64_t value) {
    mmio_write64((void*) (base + reg), value);
}

static bool hpet_check(void) {
    if (cmdline_get("nohpet") != NULL) {
        return false;
    }

    struct uacpi_table table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table);
    if (uacpi_unlikely_error(ret)) {
        return false;
    }

    bool usable = false;

    struct acpi_hpet* hpet_table = table.ptr;
    if (hpet_table->address.address_space_id != ACPI_AS_ID_SYS_MEM) {
        goto end;
    }

    usable = true;

end:
    uacpi_table_unref(&table);
    return usable;
}

// TODO: Support HPET with 32-bit main counter
static struct timer_info* hpet_init(void) {
    if (hpet_timer_info.private != NULL) {
        return &hpet_timer_info;
    }

    struct uacpi_table table;
    uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table);

    struct acpi_hpet* hpet_table = table.ptr;
    if (!(hpet_table->block_id & ACPI_HPET_COUNT_SIZE_CAP)) {
        kpanic(NULL, false, "HPET with 32-bit main counter is unsupported");
    }

    uintptr_t hpet_paddr = hpet_table->address.address;
    uintptr_t hpet_base = hpet_paddr + HIGH_VMA;

    pagemap_map(kernel_pagemap, hpet_base, hpet_paddr,
            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);

    hpet_write(hpet_base, HPET_REG_CONFIG, 0);

    uint64_t hpet_id = hpet_read(hpet_base, HPET_REG_ID);
    hpet_timer_info.hz = 1000000000000000lu / (hpet_id >> 32);
    hpet_timer_info.private = (void*) hpet_base;

    hpet_write(hpet_base, HPET_REG_COUNT, 0);
    hpet_write(hpet_base, HPET_REG_CONFIG, HPET_ENABLE_CNF);

    klog("[hpet] initialized HPET (address: 0x%lx, frequency: %luMHz, # comparators: %u)\n",
            hpet_paddr, hpet_timer_info.hz / 1000000, ((hpet_id >> 8) & 0x1f) + 1);

    uacpi_table_unref(&table);
    return &hpet_timer_info;
}

static uint64_t hpet_ticks(struct timer_info* info) {
    uintptr_t hpet_base = (uintptr_t) info->private;
    return hpet_read(hpet_base, HPET_REG_COUNT);
}

uint64_t hpet_calibrate_tsc(void) {
    uintptr_t hpet_base = (uintptr_t) hpet_timer_info.private;
    if (hpet_base == 0) {
        return 0;
    }

    uint32_t fs_per_tick = (hpet_read(hpet_base, HPET_REG_ID) >> 32) & 0xffffffff;

    uint64_t start_ticks = hpet_read(hpet_base, HPET_REG_COUNT);
    uint64_t target_ticks = start_ticks + ((TSC_CALIBRATION_TIME_MS * 1000000000000lu) / fs_per_tick);

    uint64_t tsc_start = rdtsc_serialized();
    uint64_t tsc_end;

    do {
        tsc_end = rdtsc_serialized();
    } while (hpet_read(hpet_base, HPET_REG_COUNT) < target_ticks);

    return ((tsc_end - tsc_start) / TSC_CALIBRATION_TIME_MS) * 1000;
}

struct timer_driver hpet_driver = {
    .name = "HPET",
    .priority = 50,
    .bootstrap = true,
    .check = hpet_check,
    .init = hpet_init,
    .ticks = hpet_ticks,
};
