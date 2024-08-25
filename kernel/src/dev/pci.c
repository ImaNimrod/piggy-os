#include <cpu/asm.h>
#include <dev/acpi/acpi.h>
#include <dev/pci.h>
#include <limits.h>
#include <mem/slab.h>
#include <utils/log.h>
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

/*
static struct mcfg_entry* mcfg_entries = NULL;
static size_t mcfg_entry_count = 0;
*/

static void enumerate_bus(uint8_t bus);
static void enumerate_function(uint8_t bus, uint8_t slot, uint8_t function);

static vector_t* pci_devices;

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

static const char* get_pci_class_name(uint8_t pci_class) {
    switch (pci_class) {
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

    if (pci_class > 0x13 && pci_class < 0x40) {
        return "Reserved";
    }

    return "Unassigned";
}

static void enumerate_function(uint8_t bus, uint8_t slot, uint8_t function) {
    if (legacy_io_read(bus, slot, function, PCI_CONFIG_ID) == (uint32_t) -1) {
        return;
    }

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
    dev->pci_class = (uint8_t) (class >> 24);
    dev->subclass = (uint8_t) (class >> 16);
    dev->prog_if = (uint8_t) (class >> 8);

    if ((dev->pci_class == 0x6) && (dev->subclass == 0x4)) {
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

static void enumerate_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        for (uint8_t function = 0; function < 8; function++) {
            enumerate_function(bus, slot, function);
        }
    }
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

    if (!(legacy_io_read(0, 0, 0, PCI_CONFIG_INFO) & 0x800000)) {
        enumerate_bus(0);
    } else {
        for (uint8_t function = 0; function < 8; function++) {
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
                get_pci_class_name(dev->pci_class), dev->pci_class, dev->subclass, dev->prog_if,
                dev->vendor_id, dev->device_id);

        if (dev->revision_id != 0) {
            klog(" (rev: %02u)\n", dev->revision_id);
        } else {
            klog("\n");
        }
    }
}
