#include <cpu/isr.h>
#include <dev/ioapic.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define IOREGSEL    0x00
#define IOREGWIN    0x10

#define IOAPIC_REG_ID           0x00
#define IOAPIC_REG_VERSION      0x01
#define IOAPIC_REG_ARB_ID       0x02
#define IOAPIC_REG_RENTRY_BASE  0x10

struct ioapic {
    uint8_t id;
    uintptr_t base;
    uint32_t gsi_base;
    uint8_t max_rentry;
    struct ioapic* next;
};

union ioapic_rentry {
    struct {
        uint64_t vector: 8;
        uint64_t delivery_mode: 3;
        uint64_t destination_mode: 1;
        uint64_t delivery_status: 1;
        uint64_t polarity: 1;
        uint64_t remote_irr: 1;
        uint64_t trigger_mode: 1;
        uint64_t mask: 1;
        uint64_t : 39;
        uint64_t dest: 8;
    };
    uint64_t raw;
} __attribute__((packed));

struct isa_iso {
    uint32_t gsi;
    int polarity;
    int trigger_mode;
};

static struct ioapic* ioapic_list = NULL;
static struct isa_iso* isa_isos[ISA_IRQ_NUM] = {0};

static inline uint32_t ioapic_read(uintptr_t base, uint32_t reg) {
    *((volatile uint32_t*) (base + IOREGSEL)) = reg;
    return *((volatile uint32_t*) (base + IOREGWIN));
}

static inline void ioapic_write(uintptr_t base, uint32_t reg, uint32_t value) {
    *((volatile uint32_t*) (base + IOREGSEL)) = reg;
    *((volatile uint32_t*) (base + IOREGWIN)) = value;
}

static uint64_t ioapic_read64(uintptr_t base, uint32_t reg) {
    *((volatile uint32_t*) (base + IOREGSEL)) = reg;
    uint64_t value = *((volatile uint32_t*) (base + IOREGWIN));
    *((volatile uint32_t*) (base + IOREGSEL)) = reg + 1;
    value |= ((uint64_t) *((volatile uint32_t*) (base + IOREGWIN))) << 32;
    return value;
}

static void ioapic_write64(uintptr_t base, uint32_t reg, uint64_t value) {
    *((volatile uint32_t*) (base + IOREGSEL)) = reg;
    *((volatile uint32_t*) (base + IOREGWIN)) = (uint32_t) (value & 0xffffffff);
    *((volatile uint32_t*) (base + IOREGSEL)) = reg + 1;
    *((volatile uint32_t*) (base + IOREGWIN)) = (uint32_t) ((value >> 32) & 0xffffffff);
}

static struct ioapic* get_ioapic_for_irq(uint8_t irq) {
    struct ioapic* iter;
    SLIST_FOREACH(ioapic_list, iter) {
        if (irq >= iter->gsi_base && irq < iter->gsi_base + iter->max_rentry) {
            return iter;
        }
    }
    return NULL;
}

bool ioapic_redirect_irq(uint8_t irq, uint8_t vector) {
    uint32_t gsi = irq; 
    int polarity = IOAPIC_POLARITY_ACTIVE_LOW;
    int trigger_mode = IOAPIC_TRIGGER_EDGE;

    if (irq < ISA_IRQ_NUM && isa_isos[irq] != NULL) {
        gsi = isa_isos[irq]->gsi;
        polarity = isa_isos[irq]->polarity;
        trigger_mode = isa_isos[irq]->trigger_mode;
    }

    struct ioapic* ioapic = get_ioapic_for_irq(gsi);
    if (unlikely(ioapic == NULL)) {
        return false;
    }

    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2)) };
    rentry.vector = vector;
    rentry.polarity = polarity;
    rentry.trigger_mode = trigger_mode;

    ioapic_write64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2), rentry.raw);
    return true;
}

bool ioapic_set_irq_mask(uint8_t irq, bool mask) {
    uint32_t gsi = irq; 
    if (irq < ISA_IRQ_NUM && isa_isos[irq] != NULL) {
        gsi = isa_isos[irq]->gsi;
    }

    struct ioapic* ioapic = get_ioapic_for_irq(gsi);
    if (unlikely(ioapic == NULL)) {
        return false;
    }

    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2)) };
    rentry.mask = mask & 0x01;

    ioapic_write64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2), rentry.raw);
    return true;
}

void ioapic_set_isa_iso(uint8_t irq, uint32_t gsi, int polarity, int trigger_mode) {
    if (irq >= ISA_IRQ_NUM || gsi >= ISA_IRQ_NUM) {
        return;
    }

    struct isa_iso* isa_iso = kmalloc(sizeof(struct isa_iso));
    if (unlikely(isa_iso == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ISA interrupt source override");
    }

    isa_iso->gsi = gsi;
    isa_iso->polarity = polarity;
    isa_iso->trigger_mode = trigger_mode;

    isa_isos[irq] = isa_iso;
}

void ioapic_init(uint8_t id, uintptr_t paddr, uint32_t gsi_base) {
    uintptr_t vaddr = paddr + HIGH_VMA;

    if (unlikely(!vmm_map_page(kernel_pagemap, vaddr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX))) {
        kpanic(NULL, false, "failed to create page table mapping for ioapic");
    }

    struct ioapic* ioapic = kmalloc(sizeof(struct ioapic));
    if (unlikely(ioapic == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ioapic");
    }

    ioapic->base = vaddr;
    ioapic->gsi_base = gsi_base;
    ioapic->max_rentry = (ioapic_read(vaddr, IOAPIC_REG_VERSION) >> 16) & 0xff;

    for (uint8_t i = 0; i < ioapic->max_rentry; i++) {
        union ioapic_rentry rentry = { .raw = ioapic_read64(vaddr, IOAPIC_REG_RENTRY_BASE + (i * 2)) };
        rentry.mask = true;
        ioapic_write64(vaddr, IOAPIC_REG_RENTRY_BASE + (i * 2), rentry.raw);
    }

    SLIST_PUSH_BACK(ioapic_list, ioapic);

    klog("[ioapic] initialized IOAPIC (id: %02u, address: 0x%lx, GSI base: %u)\n", id, paddr, gsi_base);
}
