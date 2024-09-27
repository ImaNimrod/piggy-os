#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <dev/acpi/acpi.h>
#include <dev/block/ata.h>
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
#define PCI_CONFIG_VENDOR_ID    PCI_CONFIG_ID
#define PCI_CONFIG_DEVICE_ID    PCI_CONFIG_ID + 2
#define PCI_CONFIG_COMMAND      0x04
#define PCI_CONFIG_STATUS       0x06
#define PCI_CONFIG_CLASS        0x08
#define PCI_CONFIG_INFO         0x0c
#define PCI_CONFIG_HEADER_TYPE  PCI_CONFIG_INFO + 2
#define PCI_CONFIG_BUS          0x18
#define PCI_CONFIG_SUBSYSTEM    0x2c
#define PCI_CONFIG_CAPABILITIES 0x34
#define PCI_CONFIG_IRQ          0x3c

#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80

#define PCI_MSI_64BIT (1 << 7)

struct mcfg_entry {
    uint64_t ecm_base_addr;
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

static struct mcfg_entry* mcfg_entries = NULL;
static size_t mcfg_entry_count = 0;

static void enumerate_bus(uint8_t bus);
static void enumerate_slot(uint8_t bus, uint8_t slot);
static void enumerate_function(uint8_t bus, uint8_t slot, uint8_t function);

static vector_t* pci_devices;

static struct pci_driver* pci_drivers[] = {
    &ata_driver,
};

static uint32_t legacy_io_read(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint8_t access_size) {
    uint32_t address =  ((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xfffc) | (1u << 31));
    outl(PCI_CONFIG_ADDR_PORT, address);

    switch (access_size) {
        case 1:
            return inb(PCI_CONFIG_DATA_PORT + (offset & 3));
        case 2:
            return inw(PCI_CONFIG_DATA_PORT + (offset & 2));
        case 4:
            return inl(PCI_CONFIG_DATA_PORT);
        default:
            return (uint32_t) -1;
    }
}

static void legacy_io_write(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value, uint8_t access_size){
    uint32_t address =  ((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xfffc) | (1u << 31));
    outl(PCI_CONFIG_ADDR_PORT, address);

    switch (access_size) {
        case 1:
            outb(PCI_CONFIG_DATA_PORT + (offset & 3), value);
            break;
        case 2:
            outw(PCI_CONFIG_DATA_PORT + (offset & 2), value);
            break;
        case 4:
            outl(PCI_CONFIG_DATA_PORT, value);
            break;
        default:
            break;
    }
}

static uint32_t ecm_read(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint8_t access_size) {
    struct mcfg_entry* entry = NULL;

    for (size_t i = 0; i < mcfg_entry_count; i++) {
        struct mcfg_entry* iter = &mcfg_entries[i];
        // legacy PCI only
        if (iter->segment == 0 && bus >= iter->bus_start && bus <= iter->bus_end) {
            entry = iter;
            break;
        }
    }

    if (entry == NULL) {
        return (uint32_t) -1;
    }

    void* addr = (void*) (((entry->ecm_base_addr + (((bus - entry->bus_start) << 20) | (slot << 15) | (function << 12))) | offset) + HIGH_VMA);

    switch (access_size) {
        case 1:
            return *(volatile uint8_t*) addr;
        case 2:
            return *(volatile uint16_t*) addr;
        case 4:
            return *(volatile uint32_t*) addr;
        default:
            return (uint32_t) -1;
    }
}

static void ecm_write(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value, uint8_t access_size) {
    struct mcfg_entry* entry = NULL;

    for (size_t i = 0; i < mcfg_entry_count; i++) {
        struct mcfg_entry* iter = &mcfg_entries[i];
        // legacy PCI only
        if (iter->segment == 0 && bus >= iter->bus_start && bus <= iter->bus_end) {
            break;
        }
    }

    if (entry == NULL) {
        return;
    }

    void* addr = (void*) (((entry->ecm_base_addr + (((bus - entry->bus_start) << 20) | (slot << 15) | (function << 12))) | offset) + HIGH_VMA);

    switch (access_size) {
        case 1:
            *(volatile uint8_t*) addr = value;
            break;
        case 2:
            *(volatile uint16_t*) addr = value;
            break;
        case 4:
            *(volatile uint32_t*) addr = value;
            break;
    }
}

uint32_t (*pci_read)(uint8_t, uint8_t, uint8_t, uint16_t, uint8_t);
void (*pci_write)(uint8_t, uint8_t, uint8_t, uint16_t, uint32_t, uint8_t);

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
            }

            next_off = PCI_READ8(dev, next_off + 1);
        }
    }

    vector_push_back(pci_devices, dev);
}

static void enumerate_slot(uint8_t bus, uint8_t slot) {
    if ((uint16_t) pci_read(bus, slot, 0, PCI_CONFIG_VENDOR_ID, 2) == 0xffff) {
        return;
    }

    enumerate_function(bus, slot, 0);

    if ((uint16_t) pci_read(bus, slot, 0, PCI_CONFIG_HEADER_TYPE, 2) & PCI_HEADER_TYPE_MULTIFUNCTION) {
        for (uint8_t function = 1; function < 8; function++) {
            if ((uint16_t) pci_read(bus, slot, function, PCI_CONFIG_VENDOR_ID, 2) == 0xffff) {
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

struct pci_bar pci_get_bar(struct pci_device* dev, uint8_t index) {
    struct pci_bar result = {0};

    if (index > 5) {
        return result;
    }

    uint16_t offset = 0x10 + (index * sizeof(uint32_t));

    uint32_t base_low = pci_read(dev->bus, dev->slot, 0, offset, 4);
    pci_write(dev->bus, dev->slot, 0, offset, 0xffffffff, 4);
    uint32_t length_low = pci_read(dev->bus, dev->slot, 0, offset, 4);
    pci_write(dev->bus, dev->slot, 0, offset, base_low, 4);

    if (base_low & 1) {
        result.base_address = base_low & ~3;
        result.length  = (~length_low & ~3) + 1;
        result.is_mmio = false;
    } else {
        int type = (base_low >> 1) & 3;
        uint32_t base_high = pci_read(dev->bus, dev->slot, 0, offset + 4, 4);

        result.base_address = base_low & 0xfffffff0;
        if (type == 2) {
            result.base_address |= ((uint64_t) base_high << 32);
        }

        result.length = ~(length_low & 0xfffffff0) + 1;
        result.is_mmio = true;
    }

    return result;
}

bool pci_setup_irq(struct pci_device* dev, uint8_t vector) {
    if (!dev->msi_supported) {
        return false;
    }

    if (dev->msi_supported) {
        uint16_t control = PCI_READ16(dev, dev->msi_offset + 2);
        uint8_t data_offset = (control & PCI_MSI_64BIT) ? 12 : 8;

        control |= 0x01;

        if ((control >> 1) & 7) {
            control &= ~0x70;
        }

        uint32_t address = 0xfee00000 | (this_cpu()->lapic_id << 12);
        uint16_t data = vector;

        PCI_WRITE16(dev, dev->msi_offset + 4, address);
        PCI_WRITE16(dev, dev->msi_offset + data_offset, data);
        PCI_WRITE16(dev, dev->msi_offset + 2, control);

        return true;
    }

    return false;
}

bool pci_mask_irq(struct pci_device* dev, bool mask) {
    if (!dev->msi_supported) {
        return false;
    }

    uint16_t control = PCI_READ16(dev, dev->msi_offset + 2);
    PCI_WRITE16(dev, dev->msi_offset + 2, mask ? control & ~0x01 : control | 0x01);
    return true;
}

void pci_write_command_flags(struct pci_device* dev, uint16_t flags) {
    uint16_t command = PCI_READ16(dev, PCI_CONFIG_COMMAND);
    command &= ~7;
    command |= flags & 7;
    PCI_WRITE16(dev, PCI_CONFIG_COMMAND, command);
}

void pci_write_prog_if(struct pci_device* dev, uint8_t prog_if) {
    uint32_t class = PCI_READ32(dev, PCI_CONFIG_CLASS);
    class = (class & 0xffff00ff) | (prog_if << 8);
    PCI_WRITE32(dev, PCI_CONFIG_CLASS, class);
    dev->prog_if = prog_if;
}

void pci_init(void) {
    pci_devices = vector_create(sizeof(struct pci_device*));
    if (pci_devices == NULL) {
        kpanic(NULL, false, "failed to create PCI device vector");
    }

    struct mcfg* mcfg = (struct mcfg*) acpi_find_sdt("MCFG");
    if (mcfg != NULL || mcfg->length < sizeof(struct mcfg) + sizeof(struct mcfg_entry)) {
        mcfg_entries = mcfg->entries;
        mcfg_entry_count = (mcfg->length - sizeof(struct mcfg)) / sizeof(struct mcfg_entry);

        klog("[pci] using ECM for device I/O\n");
        pci_read = ecm_read;
        pci_write = ecm_write;
    } else {
        klog("[pci] using legacy I/O ports for device I/O\n");
        pci_read = legacy_io_read;
        pci_write = legacy_io_write;
    }

    enumerate_bus(0);

    if ((uint16_t) pci_read(0, 0, 0, PCI_CONFIG_HEADER_TYPE, 2) & PCI_HEADER_TYPE_MULTIFUNCTION) {
        for (uint8_t function = 1; function < 8; function++) {
            if ((uint16_t) pci_read(0, 0, function, PCI_CONFIG_VENDOR_ID, 2) == 0xffff) {
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
