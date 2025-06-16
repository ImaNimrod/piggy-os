#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <dev/pci.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

#include "definitions.h"

static void reset_channel(struct ata_channel* channel) {
    outb(channel->control_base, inb(channel->control_base) | (1 << 2));
    hpet_sleep_ns(US_TO_NS(5));
    outb(channel->control_base, 0);

    uint8_t status;
    do {
        status = inb(channel->io_base + REGISTER_STATUS);
    } while ((status & (STATUS_DRQ | STATUS_BSY)) & !(status & STATUS_RDY));
}

static void ata_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct ata_channel* channel = arg;

    uint8_t busmaster_status = inb(channel->busmaster_base + BUSMASTER_REGISTER_STATUS);
    if (!(busmaster_status & BUSMASTER_STATUS_INTERRUPT)) {
        return;
    }

    if (busmaster_status & BUSMASTER_STATUS_ERROR) {
        klog("[ata] busmaster error occured\n");
    }

    outb(channel->busmaster_base + BUSMASTER_REGISTER_STATUS, busmaster_status);

    uint8_t status = inb(channel->io_base + REGISTER_STATUS);
    if (status & (STATUS_ERR | STATUS_DFT)) {
        klog("[ata] device error occured\n");
    }
}

static void ata_init(struct pci_device* pci_dev) {
    klog("[ata] found ATA controller [%04x:%04x]\n", pci_dev->vendor_id, pci_dev->device_id);

    if (!(pci_dev->prog_if & (1 << 7))) {
        klog("[ata] ATA controller does not support DMA\n");
        return;
    }

    uint8_t irq0 = ATA_ISA_IRQ0;
    uint8_t irq1 = ATA_ISA_IRQ1;

    /* determine whether the controller uses PCI or legacy ISA interrupts */
    uint8_t prog_if = pci_dev->prog_if;
    if (prog_if & 0x05) {
        uint8_t vector;
        if (unlikely(!isr_allocate_vector(&vector))) {
            kpanic(NULL, false, "failed to allocate IRQ vector for ATA controller");
        }

        if (!pci_setup_msi(pci_dev, vector)) {
            if (prog_if & 0x03) {
                prog_if &= ~(1 << 0);
            }
            if (prog_if & 0x0c) {
                prog_if &= ~(1 << 2);
            }

            if (prog_if & 0x05) {
                klog("[ata] ATA controller uses unsupported non-ISA IRQs\n");
                return;
            }

            pci_write_prog_if(pci_dev, prog_if);
        }

        if (prog_if & (1 << 0)) {
            irq0 = vector;
        }
        if (prog_if & (1 << 2)) {
            irq1 = vector;
        }
    }

    uint16_t io_base0 = 0x1f0;
    uint16_t control_base0 = 0x3f6;

    if (prog_if & (1 << 0)) {
        struct pci_bar bar0;
        pci_get_bar(pci_dev, 0, &bar0);
        io_base0 = bar0.base_address & 0xfffc;

        struct pci_bar bar1;
        pci_get_bar(pci_dev, 1, &bar1);
        control_base0 = (bar1.base_address & 0xfffc) + 2;
    }

    uint16_t io_base1 = 0x170;
    uint16_t control_base1 = 0x376;

    if (prog_if & (1 << 2)) {
        struct pci_bar bar2;
        pci_get_bar(pci_dev, 2, &bar2);
        io_base1 = bar2.base_address & 0xfffc;

        struct pci_bar bar3;
        pci_get_bar(pci_dev, 3, &bar3);
        control_base1 = (bar3.base_address & 0xfffc) + 2;
    }

    struct pci_bar bar4;
    pci_get_bar(pci_dev, 4, &bar4);
    uint16_t busmaster_base = bar4.base_address & 0xfffc;

    pci_write_command_flags(pci_dev, PCI_COMMAND_FLAG_IO_SPACE | PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER);

    uintptr_t prdt_paddr = pmm_alloc_zero(1);

    struct ata_channel* channel0 = kmalloc(sizeof(struct ata_channel));
    if (unlikely(channel0 == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ATA channel");
    }
    channel0->io_base = io_base0;
    channel0->control_base = control_base0;
    channel0->busmaster_base = busmaster_base;
    channel0->irq = irq0;
    channel0->prdt_paddr = prdt_paddr;
    channel0->dma_area_paddr = pmm_alloc_zero(1);

    struct ata_channel* channel1 = kmalloc(sizeof(struct ata_channel));
    if (unlikely(channel1 == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ATA channel");
    }
    channel1->io_base = io_base1;
    channel1->control_base = control_base1;
    channel1->busmaster_base = busmaster_base + 8;
    channel1->irq = irq1;
    channel1->prdt_paddr = prdt_paddr + 8;
    channel1->dma_area_paddr = pmm_alloc_zero(1);

    isr_register_handler(irq0, ata_irq_handler, channel0);
    isr_register_handler(irq1, ata_irq_handler, channel1);

    reset_channel(channel0);
    reset_channel(channel1);

    ata_device_identify(channel0, false);
    ata_device_identify(channel0, true);
    ata_device_identify(channel1, false);
    ata_device_identify(channel1, true);

    /* renable interrupts for both channels */
    if (irq0 == ATA_ISA_IRQ0) {
        ioapic_redirect_irq(channel0->irq, channel0->irq + ISA_IRQ_BASE);
        ioapic_set_irq_mask(channel0->irq, false);
    } else {
        pci_set_msi_mask(pci_dev, false);
    }

    if (irq1 == ATA_ISA_IRQ1) {
        ioapic_redirect_irq(channel1->irq, channel1->irq + ISA_IRQ_BASE);
        ioapic_set_irq_mask(channel1->irq, false);
    } else {
        pci_set_msi_mask(pci_dev, false);
    }
}

struct pci_driver ata_driver = {
    .init = ata_init,
    .name = "ata",
    .match_condition = PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS,
    .match_data = {
        .class = 0x01,
        .subclass = 0x01,
    },
};
