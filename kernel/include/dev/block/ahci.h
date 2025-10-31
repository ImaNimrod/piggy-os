#ifndef _KERNEL_DEV_BLOCK_AHCI_H
#define _KERNEL_DEV_BLOCK_AHCI_H

#include <dev/pci.h>

#define AHCI_DEV_MAJOR 12

extern struct pci_driver ahci_driver;

#endif /* _KERNEL_DEV_BLOCK_AHCI_H */
