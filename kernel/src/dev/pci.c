#include <cpu/asm.h>
#include <dev/acpi.h>
#include <dev/block/ahci.h>
#include <dev/block/nvme.h>
#include <dev/net/e1000.h>
#include <dev/pci.h>
#include <dev/virtio.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/vector.h>

#define PCI_CONFIG_ADDRESS_PORT 0xcf8
#define PCI_CONFIG_DATA_PORT    0xcfc

struct mcfg_entry {
    uint64_t ecm_base_address;
    uint16_t segment;
    uint8_t bus_start;
    uint8_t bus_end;
    uint32_t : 32;
} __attribute__((packed));

struct mcfg {
    struct acpi_sdt;
    uint64_t : 64;
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
        uint32_t trigger_mode : 1;
        uint32_t : 16;
    };
    uint32_t raw;
};

static struct mcfg_entry* mcfg_entries = NULL;
static size_t mcfg_entry_count = 0;

static struct slab_cache* pci_device_cache = NULL;
static vector_t* pci_devices = NULL;
static struct pci_driver* pci_drivers[] = {
    //&ahci_driver,
    &e1000_driver,
    &nvme_driver,
    &virtio_driver,
};

static uint32_t (*internal_read)(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t, uint8_t) = NULL;
static void (*internal_write)(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t, uint32_t, uint8_t) = NULL;

static void enumerate_bus(uint16_t segment, uint8_t bus);

static uint32_t ecm_read(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint8_t access_size) {
    struct mcfg_entry* entry;

    for (size_t i = 0; i < mcfg_entry_count; i++) {
        entry = &mcfg_entries[i];

        if (entry->segment == segment && bus >= entry->bus_start && bus <= entry->bus_end) {
            void* addr = (void*) (((entry->ecm_base_address + (((bus - entry->bus_start) << 20) | (slot << 15) | (function << 12))) | offset) + HIGH_VMA);

            switch (access_size) {
                case 1:
                    return mmio_read8(addr);
                case 2:
                    return mmio_read16(addr);
                case 4:
                    return mmio_read32(addr);
            }

            kpanic(NULL, false, "invalid PCI access size");
        }
    }

    kpanic(NULL, false, "unable to find ECM area for PCI device");
}

static void ecm_write(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value, uint8_t access_size) {
    struct mcfg_entry* entry;

    for (size_t i = 0; i < mcfg_entry_count; i++) {
        entry = &mcfg_entries[i];

        if (entry->segment == segment && bus >= entry->bus_start && bus <= entry->bus_end) {
            void* addr = (void*) (((entry->ecm_base_address + (((bus - entry->bus_start) << 20) | (slot << 15) | (function << 12))) | offset) + HIGH_VMA);

            switch (access_size) {
                case 1:
                    mmio_write8(addr, value);
                    break;
                case 2:
                    mmio_write16(addr, value);
                    break;
                case 4:
                    mmio_write32(addr, value);
                    break;
                default:
                    kpanic(NULL, false, "invalid PCI access size");
            }

            return;
        }
    }

    kpanic(NULL, false, "unable to find ECM area for PCI device");
}

static uint32_t legacy_read(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint8_t access_size) {
    (void) segment;

    uint32_t address =  ((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xfffc) | (1u << 31));
    outl(PCI_CONFIG_ADDRESS_PORT, address);

    switch (access_size) {
        case 1:
            return inb(PCI_CONFIG_DATA_PORT + (offset & 3));
        case 2:
            return inw(PCI_CONFIG_DATA_PORT + (offset & 2));
        case 4:
            return inl(PCI_CONFIG_DATA_PORT);
    }

    kpanic(NULL, false, "invalid PCI access size");
}

static void legacy_write(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value, uint8_t access_size){
    (void) segment;

    uint32_t address =  ((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xfffc) | (1u << 31));
    outl(PCI_CONFIG_ADDRESS_PORT, address);

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
    }

    kpanic(NULL, false, "invalid PCI access size");
}

static void enumerate_function(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function) {
    struct pci_device* dev = slab_cache_alloc(pci_device_cache);
    if (unlikely(dev == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for PCI device");
    }

    dev->segment = segment;
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;

    uint32_t id = pci_read(dev, PCI_CONFIG_ID, 4);
    dev->device_id = (uint16_t) (id >> 16);
    dev->vendor_id = (uint16_t) id;

    uint32_t class = pci_read(dev, PCI_CONFIG_CLASS, 4);
    dev->class = (uint8_t) (class >> 24);
    dev->subclass = (uint8_t) (class >> 16);
    dev->prog_if = (uint8_t) (class >> 8);
    dev->revision_id = (uint8_t) class;

    uint16_t status = pci_read(dev, PCI_CONFIG_STATUS, 2);
    if (status & (1 << 4)) {
        uint8_t next_offset = pci_read(dev, PCI_CONFIG_CAPABILITIES, 1);
        while (next_offset != 0) {
            switch (pci_read(dev, next_offset, 1)) {
                case 0x05:
                    dev->msi_supported = true;
                    dev->msi_offset = next_offset;
                    break;
                case 0x10:
                    dev->is_pcie = true;
                    break;
                case 0x11:
                    dev->msix_supported = true;
                    dev->msix_offset = next_offset;
                    break;
            }

            next_offset = pci_read(dev, next_offset + 1, 1);
        }
    }

    vector_push(pci_devices, &dev);
}

static void enumerate_slot(uint16_t segment, uint8_t bus, uint8_t slot) {
    enumerate_function(segment, bus, slot, 0);

    if ((uint16_t) internal_read(segment, bus, slot, 0, PCI_CONFIG_HEADER_TYPE, 2) & 0x80) {
        for (uint8_t function = 1; function < 8; function++) {
            if ((uint16_t) internal_read(segment, bus, slot, function, PCI_CONFIG_VENDOR_ID, 2) != 0xffff) {
                enumerate_function(segment, bus, slot, function);
            }
        }
    }
}

static void enumerate_bus(uint16_t segment, uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        if ((uint16_t) internal_read(segment, bus, slot, 0, PCI_CONFIG_VENDOR_ID, 2) != 0xffff) {
            enumerate_slot(segment, bus, slot);
        }
    }
}

uint32_t pci_read(struct pci_device* dev, uint16_t offset, uint8_t access_size) {
    return internal_read(dev->segment, dev->bus, dev->slot, dev->function, offset, access_size);
}

void pci_write(struct pci_device* dev, uint16_t offset, uint32_t value, uint8_t access_size) {
    internal_write(dev->segment, dev->bus, dev->slot, dev->function, offset, value, access_size);
}

bool pci_get_bar(struct pci_device* dev, uint8_t index, struct pci_bar* bar) {
    if (unlikely(index > 5)) {
        return false;
    }

    uint16_t offset = 0x10 + (index * 4);

    uint32_t base_low = pci_read(dev, offset, 4);

    if (base_low & 1) {
        bar->base_address = (base_low & 0xfffffffc) & 0xffff;

        pci_write(dev, offset, 0xffffffff, 4);
        bar->length = ((pci_read(dev, offset, 4) & ~(0x3)) + 1) & 0xffff;

        bar->is_mmio = false;
    } else {
        int type = (base_low >> 1) & 3;
        uint32_t base_high = pci_read(dev, offset + 4, 4);

        bar->base_address = base_low & 0xfffffff0;

        if (type == 2) {
            bar->base_address |= ((uint64_t) base_high << 32);
        }

        pci_write(dev, offset, 0xffffffff, 4);
        bar->length = ~((pci_read(dev, offset, 4) & ~(0xf))) + 1;

        bar->is_mmio = true;
        bar->mmio_prefetchable = base_low & (1 << 3);
    }

    pci_write(dev, offset, base_low, 4);
    return true;
}

bool pci_map_bar(struct pci_bar* bar) {
    if (unlikely(bar->base_address == 0 || bar->length == 0 || !bar->is_mmio)) {
        return false;
    }

    size_t page_count = DIV_CEIL(bar->length, PAGE_SIZE_4KB);

    pmm_reserve_mmio_space(bar->base_address, page_count);

    uint64_t flags = PTE_PRESENT | PTE_WRITABLE | PTE_NX;
    if (!bar->mmio_prefetchable) {
        flags |= PTE_CACHE_DISABLE;
    }

    for (size_t i = 0; i < (page_count * PAGE_SIZE_4KB); i += PAGE_SIZE_4KB) {
        pagemap_map(kernel_pagemap, bar->base_address + HIGH_VMA + i, bar->base_address + i, flags, PAGE_SIZE_4KB);
    }

    return true;
}

bool pci_setup_msi(struct pci_device* dev, uint8_t vector) {
    if (!dev->msi_supported) {
        return false;
    }

    union msi_address address = {
        .dest_id = 0,
        .base_address = 0xfee,
    };

    union msi_data data = {
        .vector = vector,
        .delivery = 0,
    };

    uint16_t control = pci_read(dev, dev->msi_offset + 2, 2);
    uint8_t data_off = (control & (1 << 7)) ? 0x0c : 0x08;
    if ((control >> 1) & 0x07) {
        control &= ~(0x07 << 4);
    }

    pci_write(dev, dev->msi_offset + 4, address.raw, 4);
    pci_write(dev, dev->msi_offset + data_off, data.raw, 2);
    pci_write(dev, dev->msi_offset + 2, control, 2);

    pci_set_command_flags(dev, PCI_COMMAND_FLAG_INTX_DISABLE, true);
    return true;
}

bool pci_set_msi_mask(struct pci_device* dev, bool mask) {
    if (!dev->msi_supported) {
        return false;
    }

    uint16_t control = pci_read(dev, dev->msi_offset + 2, 2);
    if (mask) {
        pci_write(dev, dev->msi_offset + 2, control & ~1, 2);
    } else {
        pci_write(dev, dev->msi_offset + 2, control | 1, 2);
    }

    return true;
}

bool pci_enable_msix(struct pci_device* dev) {
    if (!dev->msix_supported) {
        return false;
    }

    uint32_t info = pci_read(dev, dev->msix_offset + 4, 4);

    struct pci_bar bar;
    if (!pci_get_bar(dev, info & 7, &bar)) {
        return false;
    }
    if (!pci_map_bar(&bar)) {
        return false;
    }

    dev->msix_table = (void*) (bar.base_address + (info & ~7) + HIGH_VMA);

    uint16_t control = pci_read(dev, dev->msix_offset + 2, 2);

    uint16_t irq_count = (control & 0x3ff) + 1;
    dev->msix_irq_count = irq_count;

    for (uint16_t i = 0; i < irq_count; i++) {
        dev->msix_table[(i * 4) + 3] = 1;
    }

    pci_write(dev, dev->msix_offset + 2, (control & 0x7ff) | (1 << 15), 2);

    pci_set_command_flags(dev, PCI_COMMAND_FLAG_INTX_DISABLE, true);
    return true;
}

bool pci_setup_msix(struct pci_device* dev, uint16_t index, uint8_t vector) {
    if (!dev->msix_supported) {
        return false;
    }
    if (index >= dev->msix_irq_count) {
        return false;
    }

    union msi_address address = {
        .dest_id = 0,
        .base_address = 0xfee,
    };

    union msi_data data = {
        .vector = vector,
        .delivery = 0,
    };

    dev->msix_table[(index * 4) + 0] = address.raw;
    dev->msix_table[(index * 4) + 1] = 0;
    dev->msix_table[(index * 4) + 2] = data.raw;

    return true;
}

bool pci_set_msix_mask(struct pci_device* dev, uint16_t index, bool mask) {
    if (!dev->msix_supported) {
        return false;
    }

    if (index >= dev->msix_irq_count) {
        return false;
    }

    dev->msix_table[(index * 4) + 3] = mask ? 1 : 0;
    return true;
}

uint8_t pci_read_irq_line(struct pci_device* dev) {
    return pci_read(dev, PCI_CONFIG_IRQ, 1);
}

uint16_t pci_read_subsystem_id(struct pci_device* dev) {
    return pci_read(dev, PCI_CONFIG_SUBSYSTEM + 2, 2);
}

void pci_set_command_flags(struct pci_device* dev, uint16_t flags, bool set) {
    uint16_t command = pci_read(dev, PCI_CONFIG_COMMAND, 2);
    if (set) {
        command |= flags;
    } else {
        command &= ~flags;
    }
    pci_write(dev, PCI_CONFIG_COMMAND, command, 2);
}

void pci_init(void) {
    pci_device_cache = slab_cache_create("struct pci_device cache", sizeof(struct pci_device));
    if (unlikely(pci_device_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for pci_device structs");
    }

    pci_devices = vector_create(sizeof(struct pci_device*));
    if (unlikely(pci_devices == NULL)) {
        kpanic(NULL, false, "failed to create PCI device vector");
    }

    struct mcfg* mcfg = (struct mcfg*) acpi_find_sdt("MCFG");
    if (likely(mcfg != NULL && mcfg->length >= sizeof(struct mcfg) + sizeof(struct mcfg_entry))) {
        klog("[pci] using ECM for PCI device access\n");
        mcfg_entries = mcfg->entries;
        mcfg_entry_count = (mcfg->length - sizeof(struct mcfg)) / sizeof(struct mcfg_entry);

        internal_read = ecm_read;
        internal_write = ecm_write;

        struct mcfg_entry* entry;
        for (size_t i = 0; i < mcfg_entry_count; i++) {
            entry = &mcfg_entries[i];

            size_t page_count = (entry->bus_end - entry->bus_start) * 32 * 8;
            for (size_t j = 0; j < (page_count * PAGE_SIZE_4KB); j += PAGE_SIZE_4KB) {
                pagemap_map(kernel_pagemap, entry->ecm_base_address + HIGH_VMA + j, entry->ecm_base_address + j,
                            PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);
            }

            for (uint8_t bus = entry->bus_start; bus < entry->bus_end; bus++) {
                enumerate_bus(entry->segment, bus);
            }
        }
    } else {
        klog("[pci] using legacy I/O ports for PCI device access\n");
        internal_read = legacy_read;
        internal_write = legacy_write;

        enumerate_bus(0, 0);

        if ((uint16_t) internal_read(0, 0, 0, 0, PCI_CONFIG_HEADER_TYPE, 2) & 0x80) {
            for (uint8_t function = 1; function < 8; function++) {
                if ((uint16_t) internal_read(0, 0, 0, function, PCI_CONFIG_VENDOR_ID, 2) != 0xffff) {
                    enumerate_bus(0, function);
                }
            }
        }
    }

    klog("[pci] detected %zu devices:\n", vector_size(pci_devices));

    for (size_t i = 0; i < vector_size(pci_devices); i++) {
        struct pci_device* dev = *vector_get(pci_devices, i);

        klog(" - %02u:%02u.%u %02u:%02u:%02u [%04x:%04x]",
             dev->bus, dev->slot, dev->function,
             dev->class, dev->subclass, dev->prog_if,
             dev->vendor_id, dev->device_id);

        if (dev->revision_id != 0) {
            klog(" (rev: %02u)\n", dev->revision_id);
        } else {
            klog("\n");
        }
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(pci_drivers); i++) {
        struct pci_driver* driver = pci_drivers[i];

        for (size_t j = 0; j < vector_size(pci_devices); j++) {
            struct pci_device* dev = *vector_get(pci_devices, j);

            if (driver->match_condition & PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID) {
                if ((driver->match_condition & PCI_DRIVER_MATCH_VENDOR_ID) && (driver->match_data.vendor_id != dev->vendor_id)) {
                    continue;
                }

                if (driver->match_condition & PCI_DRIVER_MATCH_DEVICE_ID) {
                    for (size_t k = 0; k < driver->match_data.device_count; k++) {
                        if (driver->match_data.device_ids[k] == dev->device_id) {
                            driver->init(dev);
                            break;
                        }
                    }
                } else {
                    driver->init(dev);
                }
            } else if (driver->match_condition & PCI_DRIVER_MATCH_ADDRESS) {
                if ((driver->match_condition & PCI_DRIVER_MATCH_CLASS) && (driver->match_data.class != dev->class)) {
                    continue;
                }
                if ((driver->match_condition & PCI_DRIVER_MATCH_SUBCLASS) && (driver->match_data.subclass != dev->subclass)) {
                    continue;
                }
                if ((driver->match_condition & PCI_DRIVER_MATCH_PROG_IF) && (driver->match_data.prog_if != dev->prog_if)) {
                    continue;
                }

                driver->init(dev);
            }
        }
    }
}
