#ifndef _KERNEL_DEV_PCI_H
#define _KERNEL_DEV_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_COMMAND_FLAG_IO_SPACE       (1 << 0)
#define PCI_COMMAND_FLAG_MEMORY_SPACE   (1 << 1)
#define PCI_COMMAND_FLAG_BUS_MASTER     (1 << 2)

struct pci_device {
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
};

struct pci_bar {
    uintptr_t base_address;
    size_t length;
    bool is_mmio;
};

#define PCI_DRIVER_MATCH_CLASS                  (1 << 0)
#define PCI_DRIVER_MATCH_SUBCLASS               (1 << 1)
#define PCI_DRIVER_MATCH_PROG_IF                (1 << 2)
#define PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID   (1 << 3)

#define PCI_DRIVER_MATCH_ADDRESS    (PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS | PCI_DRIVER_MATCH_PROG_IF)

struct pci_driver {
    void (*init)(struct pci_device*);

    const char* name;
    int match_condition; 

    union {
        struct {
            uint16_t class;
            uint16_t subclass;
            uint16_t prog_if;
        };
        struct {
            uint16_t vendor_id;
            size_t device_count;
            uint16_t* device_ids;
        };
    } match_data;
};

struct pci_bar pci_get_bar(struct pci_device* dev, uint8_t index);
bool pci_map_bar(struct pci_bar bar);
void pci_set_command_flags(struct pci_device* dev, uint16_t flags);
bool pci_setup_irq(struct pci_device* dev, size_t index, uint8_t vector);
bool pci_mask_irq(struct pci_device* dev, size_t index, bool mask);
void pci_init(void);

#endif /* _KERNEL_DEV_PCI_H */
