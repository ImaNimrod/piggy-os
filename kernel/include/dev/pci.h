#ifndef _KERNEL_DEV_PCI_H
#define _KERNEL_DEV_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define PCI_COMMAND_FLAG_IO_SPACE       (1 << 0)
#define PCI_COMMAND_FLAG_MEMORY_SPACE   (1 << 1)
#define PCI_COMMAND_FLAG_BUSMASTER      (1 << 2)
#define PCI_COMMAND_FLAG_INTX_DISABLE   (1 << 10)

struct pci_device {
    uint16_t segment;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    uint16_t device_id;
    uint16_t vendor_id;

    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;

    bool msi_supported;
    uint8_t msi_offset;

    bool msix_supported;
    uint8_t msix_offset;
    uint32_t* msix_table;
    uint16_t msix_irq_count;

    bool is_pcie;
};

struct pci_bar {
    uintptr_t base_address;
    size_t length;
    bool is_mmio;
    bool mmio_prefetchable;
};

#define PCI_DRIVER_MATCH_CLASS                  (1 << 0)
#define PCI_DRIVER_MATCH_SUBCLASS               (1 << 1)
#define PCI_DRIVER_MATCH_PROG_IF                (1 << 2)
#define PCI_DRIVER_MATCH_ADDRESS                (PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS | PCI_DRIVER_MATCH_PROG_IF)
#define PCI_DRIVER_MATCH_VENDOR_ID              (1 << 3)
#define PCI_DRIVER_MATCH_DEVICE_ID              (1 << 4)
#define PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID   (PCI_DRIVER_MATCH_VENDOR_ID | PCI_DRIVER_MATCH_DEVICE_ID)

struct pci_driver {
    const char* name;

    int match_condition;
    union {
        struct {
            uint8_t class;
            uint8_t subclass;
            uint8_t prog_if;
        };
        struct {
            uint16_t vendor_id;
            size_t device_count;
            uint16_t* device_ids;
        };
    } match_data;

    void (*init)(struct pci_device*);
};

uint32_t pci_read(struct pci_device* dev, uint16_t offset, uint8_t access_size);
void pci_write(struct pci_device* dev, uint16_t offset, uint32_t value, uint8_t access_size);
bool pci_get_bar(struct pci_device* dev, uint8_t index, struct pci_bar* bar);
bool pci_map_bar(struct pci_bar* bar);
bool pci_setup_msi(struct pci_device* dev, uint8_t vector);
bool pci_set_msi_mask(struct pci_device* dev, bool mask);
bool pci_enable_msix(struct pci_device* dev);
bool pci_setup_msix(struct pci_device* dev, uint16_t index, uint8_t vector);
bool pci_set_msix_mask(struct pci_device* dev, uint16_t index, bool mask);
uint8_t pci_read_irq_line(struct pci_device* dev);
uint16_t pci_read_subsystem_id(struct pci_device* dev);
void pci_write_command_flags(struct pci_device* dev, uint16_t flags);
void pci_write_prog_if(struct pci_device* dev, uint8_t prog_if);
void pci_init(void);

#endif /* _KERNEL_DEV_PCI_H */
