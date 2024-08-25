#ifndef _KERNEL_DEV_PCI_H
#define _KERNEL_DEV_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    uint16_t device_id;
    uint16_t vendor_id;
    uint8_t pci_class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;

    bool msi_supported;
    uint8_t msi_offset;

    bool msix_supported;
    uint8_t msix_offset;
};

void pci_init(void);

#endif /* _KERNEL_DEV_PCI_H */
