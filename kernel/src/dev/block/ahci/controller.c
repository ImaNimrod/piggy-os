#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/hpet.h>
#include <dev/pci.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/vector.h>

#include "definitions.h"

static inline const char* interface_speed_str(uint8_t iss) {
    switch (iss) {
        case 1:
            return "generation 1 (1.5 Gbps)";	
        case 2: 
            return "generation 2 (3 Gbps)";
        case 3:
            return "generation 3 (6 Gbps)";
        default:
            return "unkown generation";
    }
}

static void enumerate_ports(struct ahci_controller* controller) {
    uint32_t pi = mmio_read32(&controller->hba_registers->pi);
    for (uint8_t i = 0; i < controller->port_count; i++) {
        if (!(pi & (1 << i))) {
            continue;
        }

        struct hba_port* hba_port = &controller->hba_registers->ports[i];

        /* switch port into idle state prior to any initialization */
        mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_ST);
        while (mmio_read32(&hba_port->cmd) & HBA_PxCMD_CR) {
            pause();
        }

        mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_FRE);
        while (mmio_read32(&hba_port->cmd) & HBA_PxCMD_FR) {
            pause();
        }

        ahci_device_try_init(controller, hba_port);
    }
}

static void ahci_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct ahci_controller* controller = arg;
    struct hba_registers* hba_registers = controller->hba_registers;

    uint32_t is = mmio_read32(&hba_registers->is);
    for (uint8_t i = 0; i < controller->port_count; i++) {
        if (is & (1 << i)) {
            struct ahci_device* device = *vector_get(controller->devices, i);
            if (likely(device != NULL)) {
                ahci_device_irq_handler(device);
            }

            mmio_write32(&hba_registers->is, mmio_read32(&hba_registers->is) | (1 << i));
        }
    }
}

static void ahci_init(struct pci_device* pci_dev) {
    klog("[ahci] found AHCI controller [%04x:%04x]\n", pci_dev->vendor_id, pci_dev->device_id);

    struct pci_bar bar5;
    if (!pci_get_bar(pci_dev, 5, &bar5)) {
        klog("[ahci] unable to get PCI BAR5 for AHCI controller ABAR\n");
        return;
    }
    if (!pci_map_bar(&bar5)) {
        klog("[ahci] failed to map memory for AHCI controller ABAR\n");
        return;
    }

    pci_write_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER);

    struct hba_registers* hba_registers = (void*) (bar5.base_address + HIGH_VMA);

    if (!(mmio_read32(&hba_registers->cap) & CAP_S64A)) {
        klog("[ahci] AHCI controller does not support 64-bit addressing\n");
        return;
    }

    int timeout;

    /* perform BIOS/OS handoff if needed */
    if (mmio_read32(&hba_registers->cap2) & (1 << 0)) {
        if (mmio_read32(&hba_registers->bohc) & BOHC_BOS) {
            mmio_write32(&hba_registers->bohc, mmio_read32(&hba_registers->bohc) | BOHC_OOS);

            timeout = 300;
            while (timeout != 0) {
                if (!(mmio_read32(&hba_registers->bohc) & (BOHC_BOS & BOHC_BB)) && mmio_read32(&hba_registers->bohc) & BOHC_OOS) {
                    break;
                }
                hpet_sleep_ns(MS_TO_NS(1));
                timeout--;
            }

            if (unlikely(timeout == 0)) {
                klog("[ahci] AHCI controller hung while performing BIOS to OS handoff\n");
                return;
            }
        }
    }

    /* reset controller */
    mmio_write32(&hba_registers->ghc, mmio_read32(&hba_registers->ghc) | GHC_HR);

    timeout = 1000;
    while (timeout != 0) {
        if (!(mmio_read32(&hba_registers->ghc) & GHC_HR)) {
            break;
        }
        hpet_sleep_ns(MS_TO_NS(1));
        timeout--;
    }

    if (unlikely(timeout == 0)) {
        klog("[ahci] AHCI controller hung while resetting\n");
        return;
    }

    /* nable AHCI mode and disable interrupts */
    mmio_write32(&hba_registers->ghc, mmio_read32(&hba_registers->ghc) | GHC_AE);
    mmio_write32(&hba_registers->ghc, mmio_read32(&hba_registers->ghc) & ~GHC_IE);

    struct ahci_controller* controller = kmalloc(sizeof(struct ahci_controller));
    if (unlikely(controller == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for AHCI controller");
    }
    controller->hba_registers = hba_registers;
    controller->devices = vector_create(sizeof(struct ahci_device*));
    if (unlikely(controller->devices == NULL)) {
        kpanic(NULL, false, "failed to create device vector for AHCI controller");
    }
    controller->port_count = (mmio_read32(&hba_registers->cap) & 0x1f) + 1;
    controller->slot_count = ((mmio_read32(&hba_registers->cap) >> 8) & 0x1f) + 1;

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for AHCI controller");
    }
    isr_register_handler(vector, ahci_irq_handler, controller);

    if (!pci_setup_msi(pci_dev, vector)) {
        klog("[ahci] failed to setup PCI interrupts for AHCI controller\n");
        goto error;
    }

    enumerate_ports(controller);
    if (vector_size(controller->devices) == 0) {
        goto error;
    }

    klog("[ahci] initialized AHCI controller (version: %x.%x, link speed: %s)\n",
         (mmio_read32(&hba_registers->vs) >> 16) & 0xffff, mmio_read32(&hba_registers->vs) & 0xffff,
         interface_speed_str((mmio_read32(&hba_registers->cap) >> 20) & 0xf));

    /* renable interrupts for the controller */
    pci_set_msi_mask(pci_dev, false);
    mmio_write32(&hba_registers->ghc, mmio_read32(&hba_registers->ghc) | GHC_IE);
    return;

error:
    isr_unregister_handler(vector);
    vector_destroy(controller->devices);
    kfree(controller);
}

struct pci_driver ahci_driver = {
    .init = ahci_init,
    .name = "ahci",
    .match_condition = PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS | PCI_DRIVER_MATCH_PROG_IF,
    .match_data = {
        .class = 0x01,
        .subclass = 0x06,
        .prog_if = 0x01,
    },
};
