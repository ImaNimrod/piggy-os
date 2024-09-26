#ifndef _KERNEL_DEV_BLOCK_ATA_H
#define _KERNEL_DEV_BLOCK_ATA_H

#include <dev/pci.h>

#define ATA_MAJ 8

extern struct pci_driver ata_driver;

#endif /* _KERNEL_DEV_BLOCK_ATA_H */
