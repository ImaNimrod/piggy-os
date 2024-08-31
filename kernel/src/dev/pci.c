#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <dev/acpi/acpi.h>
#include <dev/ata.h>
#include <dev/pci.h>
#include <limits.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/panic.h>
#include <utils/vector.h>

#define PCI_CONFIG_ADDR_PORT 0xcf8
#define PCI_CONFIG_DATA_PORT 0xcfc

#define PCI_CONFIG_ID           0x00
#define PCI_CONFIG_COMMAND      0x04
#define PCI_CONFIG_STATUS       0x06
#define PCI_CONFIG_CLASS        0x08
#define PCI_CONFIG_INFO         0x0c
#define PCI_CONFIG_BUS          0x18
#define PCI_CONFIG_SUBSYSTEM    0x2c
#define PCI_CONFIG_CAPABILITIES 0x34
#define PCI_CONFIG_IRQ          0x3c

#define PCI_READ8(DEV, OFFSET)  (uint8_t) legacy_io_read((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET))
#define PCI_READ16(DEV, OFFSET) (uint16_t) legacy_io_read((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET))
#define PCI_READ32(DEV, OFFSET) (uint32_t) legacy_io_read((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET))

#define PCI_WRITE8(DEV, OFFSET, VALUE)  legacy_io_write((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET), (VALUE), UINT8_MAX)
#define PCI_WRITE16(DEV, OFFSET, VALUE) legacy_io_write((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET), (VALUE), UINT16_MAX)
#define PCI_WRITE32(DEV, OFFSET, VALUE) legacy_io_write((DEV)->bus, (DEV)->slot, (DEV)->function, (OFFSET), (VALUE), UINT32_MAX)

struct mcfg_entry {
    uint64_t base_address;
    uint16_t segment;
    uint8_t bus_start;
    uint8_t bus_end;
    uint32_t reserved;
} __attribute__((packed));

struct mcfg {
    struct acpi_sdt;
    uint64_t reserved;
    struct mcfg_entry entries[];
} __attribute__((packed));

union msi_address {
    struct {
        uint32_t : 2;
        uint32_t dest_mode : 1;
        uint32_t redir_hint : 1;
        uint32_t : 8;
        uint32_t dest_id : 8;
        uint32_t base_address : 12;
    };

    uint32_t raw;
};

union msi_data {
    struct {
        uint32_t vector : 8;
        uint32_t delivery : 3;
        uint32_t : 3;
        uint32_t level : 1;
        uint32_t trig_mode : 1;
        uint32_t : 16;
    };

    uint32_t raw;
};

/*
static struct mcfg_entry* mcfg_entries = NULL;
static size_t mcfg_entry_count = 0;
*/

static void enumerate_bus(uint8_t bus);
static void enumerate_slot(uint8_t bus, uint8_t slot);
static void enumerate_function(uint8_t bus, uint8_t slot, uint8_t function);

static vector_t* pci_devices;

static struct pci_driver* pci_drivers[] = {
    &ata_driver,
};

static uint32_t legacy_io_read(uint8_t bus, uint8_t slot, uint8_t function, uint32_t offset) {
    uint32_t address = (1 << 31) | (offset & ~3) | ((uint32_t) function << 8) | ((uint32_t) slot << 11) | ((uint32_t) bus << 16);
    outl(PCI_CONFIG_ADDR_PORT, address);

    uint32_t value = inl(PCI_CONFIG_DATA_PORT);
    return value >>= (offset & 3) * 8;
}

static void legacy_io_write(uint8_t bus, uint8_t slot, uint8_t function, uint32_t offset, uint32_t value, uint32_t access_mask){
    uint32_t address = (1 << 31) | (offset & ~3) | ((uint32_t) function << 8) | ((uint32_t) slot << 11) | ((uint32_t) bus << 16);
    outl(PCI_CONFIG_ADDR_PORT, address);

    uint32_t old = inl(PCI_CONFIG_DATA_PORT);

    int bitoffset = (offset & 3) * 8;
    value = (value & access_mask) << bitoffset;
    old &= ~(access_mask << bitoffset);
    old |= value;

    outl(PCI_CONFIG_DATA_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, old);
}

static const char* get_class_name(uint8_t class) {
    switch (class) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge";
        case 0x07: return "Simple Communication Controller";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device Controller";
        case 0x0a: return "Docking Station";
        case 0x0b: return "Processor";
        case 0x0c: return "Serial Bus Controller";
        case 0x0d: return "Wireless Controller";
        case 0x0e: return "Intelligent Controller";
        case 0x0f: return "Satellite Communication Controller";
        case 0x10: return "Encryption Controller";
        case 0x11: return "Signal Processing Controller";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Esssential Instrumentation";
        case 0x40: return "Co-Processor";
    }

    if (class > 0x13 && class < 0x40) {
        return "Reserved";
    }

    return "Unassigned";
}

static void enumerate_function(uint8_t bus, uint8_t slot, uint8_t function) {
    struct pci_device* dev = kmalloc(sizeof(struct pci_device));
    if (dev == NULL) {
        kpanic(NULL, false, "failed to allocate memory for PCI device structure");
    }

    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;

    uint32_t id = PCI_READ32(dev, PCI_CONFIG_ID);
    dev->device_id = (uint16_t) (id >> 16);
    dev->vendor_id = (uint16_t) id;

    uint32_t class = PCI_READ32(dev, PCI_CONFIG_CLASS);
    dev->revision_id = (uint8_t) class;
    dev->class = (uint8_t) (class >> 24);
    dev->subclass = (uint8_t) (class >> 16);
    dev->prog_if = (uint8_t) (class >> 8);

    if ((dev->class == 0x6) && (dev->subclass == 0x4)) {
        uint8_t secondary_bus = (PCI_READ16(dev, PCI_CONFIG_BUS) >> 8) & 0xff;
        enumerate_bus(secondary_bus);
    }

    uint16_t status = PCI_READ16(dev, PCI_CONFIG_STATUS);
    if (status & (1 << 4)) {
        uint8_t next_off = PCI_READ8(dev, PCI_CONFIG_CAPABILITIES);

        while (next_off) {
            switch (PCI_READ8(dev, next_off)) {
                case 5:
                    dev->msi_supported = true;
                    dev->msi_offset = next_off;
                    break;
                case 17:
                    dev->msix_supported = true;
                    dev->msix_offset = next_off;
                    break;
            }

            next_off = PCI_READ8(dev, next_off + 1);
        }
    }

    vector_push_back(pci_devices, dev);
}

static void enumerate_slot(uint8_t bus, uint8_t slot) {
    if (legacy_io_read(bus, slot, 0, PCI_CONFIG_ID) == (uint32_t) -1) {
        return;
    }

    enumerate_function(bus, slot, 0);

    if (legacy_io_read(bus, slot, 0, PCI_CONFIG_INFO) & 0x800000) {
        for (uint8_t function = 1; function < 8; function++) {
            if (legacy_io_read(bus, slot, function, PCI_CONFIG_ID) == (uint32_t) -1) {
                continue;
            }

            enumerate_function(bus, slot, function);
        }
    }
}

static void enumerate_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        enumerate_slot(bus, slot);
    }
}

void pci_register_driver(struct pci_driver* driver) {
    for (size_t i = 0; i < pci_devices->size; i++) {
        struct pci_device* dev = (struct pci_device*) pci_devices->data[i];

        if (driver->match_condition & PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID) {
            if (driver->match_data.vendor_id != dev->vendor_id) {
                continue;
            }

            for (size_t j = 0; j < driver->match_data.device_count; j++) {
                if (driver->match_data.device_ids[j] == dev->device_id) {
                    driver->init(dev);
                    break;
                }
            }
        } else if (driver->match_condition & PCI_DRIVER_MATCH_ADDRESS) {
            if ((driver->match_condition & PCI_DRIVER_MATCH_CLASS)
                    && (driver->match_data.class != dev->class)) {
                continue;
            }

            if ((driver->match_condition & PCI_DRIVER_MATCH_SUBCLASS)
                    && (driver->match_data.subclass != dev->subclass)) {
                continue;
            }

            if ((driver->match_condition & PCI_DRIVER_MATCH_PROG_IF)
                    && (driver->match_data.prog_if != dev->prog_if)) {
                continue;
            }

            driver->init(dev);
        }
    }
}

void pci_set_command_flags(struct pci_device* dev, uint16_t flags) {
    uint16_t command = PCI_READ16(dev, PCI_CONFIG_COMMAND);
    command &= ~7;
    command |= flags & 7;
    PCI_WRITE16(dev, PCI_CONFIG_COMMAND, command);
}

struct pci_bar pci_get_bar(struct pci_device* dev, uint8_t index) {
    struct pci_bar result = {0};

    if (index > 5) {
        return result;
    }

    uint16_t offset = 0x10 + (index * sizeof(uint32_t));
    uint32_t base_low = PCI_READ32(dev, offset);
    PCI_WRITE32(dev, offset, 0xffffffff);
    uint32_t length_low = PCI_READ32(dev, offset);
    PCI_WRITE32(dev, offset, length_low);

    if (base_low & 1) {
        result.base_address = base_low & ~3;
        result.length  = ~(length_low & ~3) + 1;
    } else {
        int type = (base_low >> 1) & 3;
        uint32_t base_high = PCI_READ32(dev, offset + 4);

        result.base_address = base_low & 0xfffffff0;
        if (type == 2) {
            result.base_address |= ((uint64_t) base_high << 32);
        }

        result.length = ~(length_low & 0xfffffff0) + 1;
        result.is_mmio = true;
    }

    return result;
}

bool pci_map_bar(struct pci_bar bar) {
    if (!bar.is_mmio) {
        return false;
    }

    uintptr_t start = ALIGN_DOWN(bar.base_address, PAGE_SIZE);
    uintptr_t end = ALIGN_UP(bar.base_address + bar.length, PAGE_SIZE);

    bool mapped = true;

    for (size_t i = 0; i < DIV_CEIL(end - start, PAGE_SIZE) && mapped; i++) {
        if (vmm_get_page_mapping(kernel_pagemap, start + (i * PAGE_SIZE)) == (uintptr_t) -1) {
            mapped = false;
        }
    }

    if (!mapped) {
        for (uintptr_t ptr = start; ptr < end; ptr += PAGE_SIZE) {
            vmm_unmap_page(kernel_pagemap, ptr);
            vmm_unmap_page(kernel_pagemap, ptr + HIGH_VMA);

            if (!vmm_map_page(kernel_pagemap, ptr, ptr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_NX) ||
                    !vmm_map_page(kernel_pagemap, ptr + HIGH_VMA, ptr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_NX)) {
                return false;
            }
        }
    }

    return true;
}

bool pci_setup_irq(struct pci_device* dev, size_t index, uint8_t vector) {
    union msi_address address = { .dest_id = this_cpu()->lapic_id, .base_address = 0xfee };  
    union msi_data data = { .vector = vector, .delivery = 0 };

    if (dev->msix_supported) {
        uint16_t control = PCI_READ16(dev, dev->msix_offset + 2);
        control |= (3 << 14);
        PCI_WRITE16(dev, dev->msix_offset + 2, control);

        uint16_t num_irqs = (control & ((1 << 11) - 1)) + 1;
        uint32_t info = PCI_READ32(dev, dev->msix_offset + 4);
        if (index > num_irqs) {
            return false;
        }

        struct pci_bar bar = pci_get_bar(dev, info & 7);
        if (bar.base_address == 0 || !bar.is_mmio) {
            return false;
        }

        uintptr_t target = bar.base_address + (info & ~7) + HIGH_VMA;
        target += index * 16;
        ((volatile uint64_t*) target)[0] = address.raw;
        ((volatile uint32_t*) target)[2] = data.raw;

        ((volatile uint32_t*) target)[3] = 0;
        PCI_WRITE16(dev, dev->msix_offset + 2, control & ~(1 << 14));
    } else if (dev->msi_supported) {
        uint16_t control = PCI_READ16(dev, dev->msi_offset + 2) | 1;
        uint8_t data_off = (control & (1 << 7)) ? 12 : 8;

        if ((control >> 1) & 7) {
            control &= ~(7 << 4);
        }

        PCI_WRITE32(dev, dev->msi_offset + 4, address.raw);
        PCI_WRITE16(dev, dev->msi_offset + data_off, data.raw);
        PCI_WRITE16(dev, dev->msi_offset + 2, control);
    } else {
        return false;
    }

    return true;
}

bool pci_mask_irq(struct pci_device* dev, size_t index, bool mask) {
    if (dev->msix_supported) {
        uint16_t control = PCI_READ16(dev, dev->msix_offset + 2);
        uint16_t num_irqs  = (control & ((1 << 11) - 1)) + 1;
        uint32_t info = PCI_READ32(dev, dev->msix_offset + 4);
        if (index > num_irqs) {
            return false;
        }

        struct pci_bar bar = pci_get_bar(dev, info & 7);
        if (bar.base_address == 0 || !bar.is_mmio) {
            return false;
        }

        uintptr_t target = bar.base_address + (info & ~7) + HIGH_VMA;
        target += index * 16;

        ((volatile uint32_t*) target)[3] = (int) mask;
    } if (dev->msi_supported) {
        uint16_t control = PCI_READ16(dev, dev->msi_offset + 2);
        if (mask) {
            PCI_WRITE16(dev, dev->msi_offset + 2, control & ~1);
        } else {
            PCI_WRITE16(dev, dev->msi_offset + 2, control | 1);
        }
    } else {
        return false;
    }

    return true;
}

void pci_init(void) {
    pci_devices = vector_create(sizeof(struct pci_device*));
    if (pci_devices == NULL) {
        kpanic(NULL, false, "failed to create PCI device vector");
    }

    struct mcfg* mcfg = (struct mcfg*) acpi_find_sdt("MCFG");
    if (mcfg != NULL) {
        // TODO: implement new PCIE ECAM usage
    }

    enumerate_bus(0);

    if (legacy_io_read(0, 0, 0, PCI_CONFIG_INFO) & 0x800000) {
        for (uint8_t function = 1; function < 8; function++) {
            if (legacy_io_read(0, 0, function, PCI_CONFIG_ID) == (uint32_t) -1) {
                continue;
            }

            enumerate_bus(function);
        }
    }

    klog("[pci] detected %u devices:\n", pci_devices->size);

    for (size_t i = 0; i < pci_devices->size; i++) {
        struct pci_device* dev = (struct pci_device*) pci_devices->data[i];
        klog(" - %02u:%02u.%u %s %02u:%02u:%02u [%04x:%04x]",
                dev->bus, dev->slot, dev->function,
                get_class_name(dev->class), dev->class, dev->subclass, dev->prog_if,
                dev->vendor_id, dev->device_id);

        if (dev->revision_id != 0) {
            klog(" (rev: %02u)\n", dev->revision_id);
        } else {
            klog("\n");
        }
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(pci_drivers); i++) {
        struct pci_driver* driver = pci_drivers[i];

        for (size_t j = 0; j < pci_devices->size; j++) {
            struct pci_device* dev = (struct pci_device*) pci_devices->data[j];

            if (driver->match_condition & PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID) {
                if (driver->match_data.vendor_id != dev->vendor_id) {
                    continue;
                }

                for (size_t k = 0; k < driver->match_data.device_count; k++) {
                    if (driver->match_data.device_ids[k] == dev->device_id) {
                        driver->init(dev);
                        break;
                    }
                }
            } else if (driver->match_condition & PCI_DRIVER_MATCH_ADDRESS) {
                if ((driver->match_condition & PCI_DRIVER_MATCH_CLASS)
                        && (driver->match_data.class != dev->class)) {
                    continue;
                }

                if ((driver->match_condition & PCI_DRIVER_MATCH_SUBCLASS)
                        && (driver->match_data.subclass != dev->subclass)) {
                    continue;
                }

                if ((driver->match_condition & PCI_DRIVER_MATCH_PROG_IF)
                        && (driver->match_data.prog_if != dev->prog_if)) {
                    continue;
                }

                driver->init(dev);
            }
        }
    }
}
