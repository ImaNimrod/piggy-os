#include <cpu/asm.h>
#include <cpu/ioapic.h>
#include <cpu/isr.h>
#include <dev/hpet.h>
#include <mem/paging.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

#define HPET_REG_ID                     0x000
#define HPET_REG_CONFIG                 0x010
#define HPET_REG_ISR                    0x020
#define HPET_REG_COUNT                  0x0f0
#define HPET_REG_TIMER_CONFIG(n)        (0x100 + 0x20 * n)
#define HPET_REG_TIMER_COMPARATOR(n)    (0x108 + 0x20 * n)

#define HPET_ENABLE_CNF         (1 << 0)
#define HPET_LEG_RT_CNF         (1 << 1)

#define HPET_LEG_RT_CAP         (1 << 15)

#define HPET_Tn_INT_TYPE_CNF    (1 << 1)
#define HPET_Tn_INT_ENB_CNF     (1 << 2)
#define HPET_Tn_TYPE_CNF        (1 << 3)
#define HPET_Tn_VAL_SET_CNF     (1 << 6)
#define HPET_Tn_32MODE_CNF      (1 << 8)
#define HPET_Tn_FSB_EN_CNF      (1 << 14)

#define PIT_ISA_IRQ 0

#define TSC_CALIBRATION_TIME_US 500

typedef enum {
    HPET_TYPE_LEGACY,
    HPET_TYPE_64BIT,
} hpet_type_t;

static spinlock_t hpet_init_lock;
static struct timer_info hpet_timer_info;

static struct {
    uintptr_t base_addr;
    uint64_t ticks_passed;
    int type;
} hpet_private;

static inline uint64_t hpet_read(uint32_t reg) {
    return mmio_read64((void*) (hpet_private.base_addr + reg));
}

static inline void hpet_write(uint32_t reg, uint64_t value) {
    mmio_write64((void*) (hpet_private.base_addr + reg), value);
}

static void hpet_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;

    hpet_write(HPET_REG_CONFIG, 0);

    atomic_fetch_add_explicit(&hpet_private.ticks_passed, hpet_read(HPET_REG_COUNT), memory_order_seq_cst);
    hpet_write(HPET_REG_COUNT, 0);

    hpet_write(HPET_REG_CONFIG, HPET_ENABLE_CNF | ((hpet_private.type == HPET_TYPE_LEGACY) ? HPET_LEG_RT_CNF : 0));
}

static bool hpet_check(void) {
    if (cmdline_get("nohpet")) {
        return false;
    }

    spinlock_acquire(&hpet_init_lock);

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
    spinlock_release(&hpet_init_lock);
    return usable;
}

static struct timer_info* hpet_init(void) {
    spinlock_acquire(&hpet_init_lock);

    if (hpet_timer_info.private) {
        goto end;
    }

    struct uacpi_table table;
    uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table);

    struct acpi_hpet* hpet_table = table.ptr;

    uintptr_t hpet_paddr = hpet_table->address.address;
    hpet_private.base_addr = hpet_paddr + HIGH_VMA;

    pagemap_map(kernel_pagemap, hpet_paddr + HIGH_VMA, hpet_paddr,
            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);

    hpet_write(HPET_REG_CONFIG, 0);

    uint64_t hpet_id = hpet_read(HPET_REG_ID);

    if (!(hpet_table->block_id & ACPI_HPET_COUNT_SIZE_CAP)) {
        if (!(hpet_id & HPET_LEG_RT_CAP)) {
            kpanic(NULL, false, "32-bit HPET without legacy replacement mode detected");
        }

        hpet_private.type = HPET_TYPE_LEGACY;

        // If we have to use legacy replacement mode, disable FSB mode, enable interrupts,
        // setup timer in periodic mode, and force 32-bit operation
        uint64_t timer_config = hpet_read(HPET_REG_TIMER_CONFIG(0));
        timer_config &= ~(HPET_Tn_INT_TYPE_CNF | HPET_Tn_FSB_EN_CNF);
        timer_config |= (HPET_Tn_INT_ENB_CNF | HPET_Tn_TYPE_CNF | HPET_Tn_VAL_SET_CNF | HPET_Tn_32MODE_CNF);

        // We use a very long time for periodic trigger just to ensure that the 32-bit counter doesn't ever overflow
        hpet_write(HPET_REG_TIMER_CONFIG(0), timer_config);
        hpet_write(HPET_REG_TIMER_COMPARATOR(0), 0x80000000);

        isr_register_handler(PIT_ISA_IRQ + ISA_IRQ_BASE, hpet_irq_handler, NULL);
        ioapic_redirect_irq(PIT_ISA_IRQ, PIT_ISA_IRQ + ISA_IRQ_BASE);
        ioapic_set_irq_mask(PIT_ISA_IRQ, false);

        klog("[hpet] 32-bit timer detected, running HPET in legacy replacement mode\n");
    } else {
        hpet_private.type = HPET_TYPE_64BIT;
    }

    hpet_write(HPET_REG_COUNT, 0);
    hpet_write(HPET_REG_CONFIG, HPET_ENABLE_CNF | ((hpet_private.type == HPET_TYPE_LEGACY) ? HPET_LEG_RT_CNF : 0));

    hpet_timer_info.hz = 1000000000000000lu / (hpet_id >> 32);
    hpet_timer_info.private = &hpet_private;

    klog("[hpet] initialized HPET (address: 0x%lx, frequency: %luMHz, # comparators: %u)\n",
            hpet_paddr, hpet_timer_info.hz / 1000000, ((hpet_id >> 8) & 0x1f) + 1);

    uacpi_table_unref(&table);

end:
    spinlock_release(&hpet_init_lock);
    return &hpet_timer_info;
}

static uint64_t hpet_ticks(struct timer_info* info) {
    (void) info;

    if (hpet_private.type == HPET_TYPE_64BIT) {
        return hpet_read(HPET_REG_COUNT);
    }

    uint64_t ticks;
    do {
        ticks = __atomic_load_n(&hpet_private.ticks_passed, __ATOMIC_SEQ_CST) + (hpet_read(HPET_REG_COUNT) & 0xffffffff);
    } while (ticks < __atomic_load_n(&hpet_private.ticks_passed, __ATOMIC_SEQ_CST));
    return ticks;
}

uint64_t hpet_calibrate_tsc(void) {
    if (!hpet_timer_info.private) {
        return 0;
    }

    uint32_t fs_per_tick = (hpet_read(HPET_REG_ID) >> 32) & 0xffffffff;

    uint64_t start_ticks = hpet_ticks(&hpet_timer_info);
    uint64_t target_ticks = start_ticks + ((TSC_CALIBRATION_TIME_US * 1000000000lu) / fs_per_tick);

    uint64_t tsc_start = rdtsc_serialized();
    uint64_t tsc_end;

    do {
        tsc_end = rdtsc_serialized();
    } while (hpet_ticks(&hpet_timer_info) < target_ticks);

    return ((tsc_end - tsc_start) / TSC_CALIBRATION_TIME_US) * 1000000;
}

struct timer_driver hpet_driver = {
    .name = "HPET",
    .priority = 50,
    .bootstrap = true,
    .check = hpet_check,
    .init = hpet_init,
    .ticks = hpet_ticks,
};
