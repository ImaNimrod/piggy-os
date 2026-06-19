#include <cpu/asm.h>
#include <cpu/ioapic.h>
#include <cpu/isr.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>

#define IOREGSEL 0x00
#define IOREGWIN 0x10

#define IOAPIC_REG_ID           0x00
#define IOAPIC_REG_VERSION      0x01
#define IOAPIC_REG_ARB_ID       0x02
#define IOAPIC_REG_RENTRY_BASE  0x10

#define IOAPIC_POLARITY_ACTIVE_HIGH 0
#define IOAPIC_POLARITY_ACTIVE_LOW  1

#define IOAPIC_TRIGGER_MODE_EDGE    0
#define IOAPIC_TRIGGER_MODE_LEVEL   1

#define PIC1_COMMAND_PORT       0x20
#define PIC1_DATA_PORT          0x21
#define PIC2_COMMAND_PORT       0xa0
#define PIC2_DATA_PORT          0xa1

struct ioapic {
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
    bool init;
    uint32_t gsi;
    int polarity;
    int trigger_mode;
};

static struct ioapic* ioapic_list;
static struct isa_iso isa_isos[ISA_IRQ_NUM];

static inline uint32_t ioapic_read(uintptr_t base, uint32_t reg) {
    mmio_write32((void*) (base + IOREGSEL), reg);
    mfence();
    return mmio_read32((void*) (base + IOREGWIN));
}

static inline void ioapic_write(uintptr_t base, uint32_t reg, uint32_t value) {
    mmio_write32((void*) (base + IOREGSEL), reg);
    mmio_write32((void*) (base + IOREGWIN), value);
    mfence();
}

static inline uint64_t ioapic_read64(uintptr_t base, uint32_t reg) {
    mmio_write32((void*) (base + IOREGSEL), reg);
    mfence();
    uint64_t value = mmio_read32((void*) (base + IOREGWIN));
    mmio_write32((void*) (base + IOREGSEL), reg + 1);
    mfence();
    value |= ((uint64_t) mmio_read32((void*) (base + IOREGWIN))) << 32;
    return value;
}

static inline void ioapic_write64(uintptr_t base, uint32_t reg, uint64_t value) {
    mmio_write32((void*) (base + IOREGSEL), reg);
    mmio_write32((void*) (base + IOREGWIN), (uint32_t) value);
    mfence();
    mmio_write32((void*) (base + IOREGSEL), reg + 1);
    mmio_write32((void*) (base + IOREGWIN), (uint32_t) (value >> 32));
    mfence();
}

static struct ioapic* get_ioapic_for_irq(uint8_t irq) {
    struct ioapic* iter;
    SLIST_FOREACH(ioapic_list, iter, next) {
        if (irq >= iter->gsi_base && irq < iter->gsi_base + iter->max_rentry) {
            return iter;
        }
    }
    return NULL;
}

static void legacy_pic_disable(void) {
    // Mask all PIC interrupts
    outb(PIC1_DATA_PORT, 0xff);
    outb(PIC2_DATA_PORT, 0xff);

    // Remap PIC interrupts to 0x20 - 0x30 to avoid conflicts with builtin CPU exceptions
    outb(PIC1_COMMAND_PORT, 0x11);
    outb(PIC2_COMMAND_PORT, 0x11);
    outb(PIC1_DATA_PORT, 0x20);
    outb(PIC2_DATA_PORT, 0x28);
    outb(PIC1_DATA_PORT, 0x04);
    outb(PIC2_DATA_PORT, 0x02);
    outb(PIC1_DATA_PORT, 0x01);
    outb(PIC2_DATA_PORT, 0x01);
}

static void parse_ioapic_entry(struct acpi_madt_ioapic* ioapic_entry) {
    uintptr_t vaddr = ioapic_entry->address + HIGH_VMA;

    pagemap_map(kernel_pagemap, vaddr, ioapic_entry->address,
            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);

    struct ioapic* ioapic = kmalloc(sizeof(struct ioapic));
    if (unlikely(ioapic == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ioapic");
    }
    ioapic->base = vaddr;
    ioapic->gsi_base = ioapic_entry->gsi_base;
    ioapic->max_rentry = (ioapic_read(vaddr, IOAPIC_REG_VERSION) >> 16) & 0xff;

    for (uint8_t i = 0; i < ioapic->max_rentry; i++) {
        union ioapic_rentry rentry = { .raw = ioapic_read64(vaddr, IOAPIC_REG_RENTRY_BASE + (i * 2)) };
        rentry.mask = true;
        ioapic_write64(vaddr, IOAPIC_REG_RENTRY_BASE + (i * 2), rentry.raw);
    }

    SLIST_PUSH_FRONT(ioapic_list, ioapic, next);

    klog("[ioapic] initialized IOAPIC (id: %02u, address: 0x%lx, GSI base: %u)\n",
            ioapic_entry->id, ioapic_entry->address, ioapic_entry->gsi_base);
}

static void parse_iso_entry(struct acpi_madt_interrupt_source_override* iso_entry) {
    if (iso_entry->bus != 0) {
        return;
    }

    if (iso_entry->source >= ISA_IRQ_NUM || iso_entry->gsi >= ISA_IRQ_NUM) {
        return;
    }

    int polarity;
    int trigger_mode;

    uint8_t polarity_flags = iso_entry->flags & ACPI_MADT_POLARITY_MASK;
    if (polarity_flags == ACPI_MADT_POLARITY_CONFORMING || polarity_flags == ACPI_MADT_POLARITY_ACTIVE_HIGH) {
        polarity = IOAPIC_POLARITY_ACTIVE_HIGH;
    } else if (polarity_flags == ACPI_MADT_POLARITY_ACTIVE_LOW) {
        polarity = IOAPIC_POLARITY_ACTIVE_LOW;
    } else {
        kpanic(NULL, false, "invalid polarity flags in interrupt source override");
    }

    uint8_t trigger_mode_flags = iso_entry->flags & ACPI_MADT_TRIGGERING_MASK;
    if (trigger_mode_flags == ACPI_MADT_TRIGGERING_CONFORMING || trigger_mode_flags == ACPI_MADT_TRIGGERING_EDGE) {
        trigger_mode = IOAPIC_TRIGGER_MODE_EDGE;
    } else if (trigger_mode_flags == ACPI_MADT_TRIGGERING_LEVEL) {
        trigger_mode = IOAPIC_TRIGGER_MODE_LEVEL;
    } else {
        kpanic(NULL, false, "invalid trigger mode flags in interrupt source override");
    }

    isa_isos[iso_entry->source] = (struct isa_iso) { true, iso_entry->gsi, polarity, trigger_mode };

    klog("[ioapic] mapping ISA IRQ %-2u -> GSI %u\n",
            iso_entry->source, iso_entry->gsi);
}

static uacpi_iteration_decision parse_madt(void* user, struct acpi_entry_hdr* entry) {
    (void) user;

    switch (entry->type) {
        case ACPI_MADT_ENTRY_TYPE_IOAPIC:
            parse_ioapic_entry((struct acpi_madt_ioapic*) entry);
            break;
        case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE:
            parse_iso_entry((struct acpi_madt_interrupt_source_override*) entry);
            break;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

bool ioapic_redirect_irq(uint8_t irq, uint8_t vector) {
    uint32_t gsi = irq; 
    int polarity = IOAPIC_POLARITY_ACTIVE_HIGH;
    int trigger_mode = IOAPIC_TRIGGER_MODE_EDGE;

    if (irq < ISA_IRQ_NUM && isa_isos[irq].init) {
        gsi = isa_isos[irq].gsi;
        polarity = isa_isos[irq].polarity;
        trigger_mode = isa_isos[irq].trigger_mode;
    }

    struct ioapic* ioapic = get_ioapic_for_irq(gsi);
    if (unlikely(ioapic == NULL)) {
        return false;
    }

    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2)) };
    rentry.vector = vector;
    rentry.delivery_mode = 0;
    rentry.destination_mode = 0;
    rentry.polarity = polarity;
    rentry.trigger_mode = trigger_mode;

    ioapic_write64(ioapic->base, IOAPIC_REG_RENTRY_BASE + (gsi * 2), rentry.raw);
    return true;
}

bool ioapic_set_irq_mask(uint8_t irq, bool mask) {
    uint32_t gsi = irq; 
    if (irq < ISA_IRQ_NUM && isa_isos[irq].init) {
        gsi = isa_isos[irq].gsi;
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

void ioapic_init(void) {
    struct uacpi_table table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "unable to find MADT table: %s", uacpi_status_to_string(ret));
    }

    struct acpi_madt* madt_table = table.ptr;
    if (likely(madt_table->flags & ACPI_PCAT_COMPAT)) {
        legacy_pic_disable();
        klog("[ioapic] disabled legacy 8259 PIC\n");
    }

    uacpi_for_each_subtable(table.hdr, sizeof(struct acpi_madt), parse_madt, NULL);
    uacpi_table_unref(&table);
}
