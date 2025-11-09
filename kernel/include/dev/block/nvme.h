#ifndef _KERNEL_DEV_BLOCK_NVME_H
#define _KERNEL_DEV_BLOCK_NVME_H

#include <dev/pci.h>

#define NVME_DEV_MAJOR 15

extern struct pci_driver nvme_driver;

#endif /* _KERNEL_DEV_BLOCK_NVME_H */
