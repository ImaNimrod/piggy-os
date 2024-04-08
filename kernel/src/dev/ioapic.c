#include <dev/ioapic.h>
#include <mem/heap.h>
#include <utils/list.h>
#include <utils/log.h>

#define IOREGSEL 0x00
#define IOREGWIN 0x10

#define IOAPIC_ID           0x00
#define IOAPIC_VERSION      0x01
#define IOAPIC_ARB_ID       0x02
#define IOAPIC_RENTRY_BASE  0x10

#define IOAPIC_POLARITY_ACTIVE_HIGH 0
#define IOAPIC_POLARITY_ACTIVE_LOW  1

#define IOAPIC_TRIGGER_EDGE  0
#define IOAPIC_TRIGGER_LEVEL 1

struct ioapic_device {
    uint8_t id;
    uint8_t max_rentry;
    uint8_t version;
    uintptr_t address;
    uint32_t gsi_base;
    LIST_ENTRY(struct ioapic_device) list;
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

static LIST_HEAD(struct ioapic_device) ioapics;

static inline uint32_t get_ioapic_rentry_index(uint8_t irq) {
    return (IOAPIC_RENTRY_BASE + (irq * 2));
}

static inline uint32_t ioapic_read(uintptr_t ioapic_base, uint32_t reg) {
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg;
    return *((volatile uint32_t*) (ioapic_base + IOREGWIN));
}

static uint64_t ioapic_read64(uintptr_t ioapic_base, uint32_t reg) {
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg;
    uint64_t value = *((volatile uint32_t*) (ioapic_base + IOREGWIN));
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg + 1;
    value |= ((uint64_t) *((volatile uint32_t*) (ioapic_base + IOREGWIN))) << 32;
    return value;
}

static inline void ioapic_write(uintptr_t ioapic_base, uint32_t reg, uint32_t value) {
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg;
    *((volatile uint32_t*) (ioapic_base + IOREGWIN)) = value;
}

static void ioapic_write64(uintptr_t ioapic_base, uint32_t reg, uint64_t value) {
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg;
    *((volatile uint32_t*) (ioapic_base + IOREGWIN)) = (uint32_t) (value & 0xffffffff);
    *((volatile uint32_t*) (ioapic_base + IOREGSEL)) = reg + 1;
    *((volatile uint32_t*) (ioapic_base + IOREGWIN)) = (uint32_t) ((value >> 32) & 0xffffffff);
}

static struct ioapic_device* get_ioapic_by_id(uint8_t id) {
    struct ioapic_device* ioapic;
    LIST_FOREACH(ioapic, &ioapics, list) {
        if (ioapic->id == id) {
            return ioapic;
        }
    }

    return NULL;
}

static struct ioapic_device* get_ioapic_for_interrupt(uint8_t irq) {
    struct ioapic_device* ioapic;
    LIST_FOREACH(ioapic, &ioapics, list) {
        if (irq >= ioapic->gsi_base && irq < ioapic->gsi_base + ioapic->max_rentry) {
            return ioapic;
        }
    }

    return NULL;
}

void ioapic_set_irq_mask(uint8_t irq, bool mask) {
    struct ioapic_device* ioapic = get_ioapic_for_interrupt(irq);
    if (!ioapic) {
        kpanic("ioapic_set_irq_mask: no ioapic for IRQ%u", irq);
    }
    
    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->address, get_ioapic_rentry_index(irq)) };
    rentry.mask = mask & 1;

    ioapic_write64(ioapic->address, get_ioapic_rentry_index(irq), rentry.raw);
}

void ioapic_set_irq_vector(uint8_t irq, uint8_t vector) {
    struct ioapic_device* ioapic = get_ioapic_for_interrupt(irq);
    if (!ioapic) {
        kpanic("ioapic_set_irq_vector: no ioapic for IRQ%u", irq);
    }

    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->address, get_ioapic_rentry_index(irq)) };
    rentry.trigger_mode = IOAPIC_TRIGGER_EDGE;
    rentry.vector = vector;

    ioapic_write64(ioapic->address, get_ioapic_rentry_index(irq), rentry.raw);
}

void ioapic_set_isa_irq_routing(uint8_t isa_irq, uint8_t vector, uint16_t flags) {
    if (isa_irq >= 16) {
        kpanic("ioapic_set_isa_irq_routing: ISA IRQ%u out of range", isa_irq);
    }

    struct ioapic_device* ioapic = get_ioapic_for_interrupt(isa_irq);
    if (!ioapic) {
        kpanic("ioapic_set_isa_irq_routing: no ioapic for IRQ%u", isa_irq);
    }

    union ioapic_rentry rentry = { .raw = ioapic_read64(ioapic->address, get_ioapic_rentry_index(isa_irq)) };
    rentry.mask = true;
    rentry.vector = vector;

    uint8_t polarity = flags & 0x03;
    if (polarity == 0x00 || polarity == 0x03) {
        rentry.polarity = IOAPIC_POLARITY_ACTIVE_LOW;
    } else if (polarity == 0x01) {
        rentry.polarity = IOAPIC_POLARITY_ACTIVE_HIGH;
    } else {
        kpanic("ioapic_set_isa_irq_routing: invalid pin polarity %u", polarity);
    }

    uint8_t trigger_mode = (flags >> 2) & 0x03;
    if (trigger_mode == 0x00 || trigger_mode == 0x01) {
        rentry.trigger_mode = IOAPIC_TRIGGER_EDGE;
    } else if (trigger_mode == 0x03) {
        rentry.trigger_mode = IOAPIC_TRIGGER_LEVEL;
    } else {
        kpanic("iopaic_set_isa_irq_routing: invalid trigger mode %u", trigger_mode);
    }

    ioapic_write64(ioapic->address, get_ioapic_rentry_index(isa_irq), rentry.raw);
}

void register_ioapic(uint8_t id, uintptr_t address, uint32_t gsi_base) {
    klog("[ioapic] initializing ioapic #%u (address=0x%lx, GSI base=%u)\n", id, address, gsi_base);

    struct ioapic_device* ioapic = kmalloc(sizeof(struct ioapic_device));
    ioapic->id = id;
    ioapic->address = address;
    ioapic->gsi_base = gsi_base;

    uint32_t version = ioapic_read(address, IOAPIC_VERSION);
    ioapic->max_rentry = (version >> 16) & 0xff;
    ioapic->version = version & 0xff;

    for (uint8_t i = 0; i <= ioapic->max_rentry; i++) {
        union ioapic_rentry rentry = { .raw = ioapic_read64(address, get_ioapic_rentry_index(i)) };
        rentry.mask = true;
        ioapic_write64(address, get_ioapic_rentry_index(i), rentry.raw);
    }

    LIST_ADD_FRONT(&ioapics, ioapic, list);
}
