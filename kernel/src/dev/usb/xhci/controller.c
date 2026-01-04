#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/hpet.h>
#include <dev/usb/xhci.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"

// TODO: support multiple interrupters or maybe just commit sepuku instead of doing USB :)

#define CMD_TRB_COUNT (PAGE_SIZE_4KB / sizeof(struct trb))

struct xhci_ring {
    struct trb* trbs;
    uint32_t index;
    bool cycle;
};

struct xhci_controller {
    struct capability_registers* capability_registers;
    struct operational_registers* operational_registers;
    struct runtime_registers* runtime_registers;
    uint32_t* doorbell_registers;

    uint32_t command_cycle;

    uint8_t port_count;
    uint8_t slot_count;

    struct xhci_ring command_ring;
    struct xhci_ring event_ring;
};

static void submit_cmd(struct xhci_controller* controller, struct trb* cmd) {
    struct xhci_ring* command_ring = &controller->command_ring;

    cmd->cycle = command_ring->cycle ? 1 : 0;
    command_ring->trbs[command_ring->index++] = *cmd;

    if (command_ring->index == CMD_TRB_COUNT - 1) {
        command_ring->trbs[CMD_TRB_COUNT - 1].control = (6 << 10) | (1 << 1) | (command_ring->cycle ? 1 : 0);
        command_ring->index = 0;
        command_ring->cycle = !command_ring->cycle;
    }

    mmio_write32(&controller->doorbell_registers[0], 0);
}

static void xhci_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct xhci_controller* controller = arg;
    klog("irq\n");
}

static void xhci_init(struct pci_device* pci_dev) {
    klog("[xhci] found xHCI controller [%04x:%04x]\n", pci_dev->vendor_id, pci_dev->device_id);

    struct pci_bar bar0;
    if (!pci_get_bar(pci_dev, 0, &bar0)) {
        klog("[xhci] unable to get PCI BAR0 for xHCI controller configuration space\n");
        return;
    }
    if (!pci_map_bar(&bar0)) {
        klog("[xhci] failed to map memory for xHCI controller configuration space\n");
        return;
    }

    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER | PCI_COMMAND_FLAG_INTX_DISABLE, true);
    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_IO_SPACE, false);

    struct capability_registers* capability_registers = (void*) (bar0.base_address + HIGH_VMA);
    struct operational_registers* operational_registers = (void*) ((uintptr_t) capability_registers + mmio_read8(&capability_registers->caplength));
    struct runtime_registers* runtime_registers = (void*) ((uintptr_t) capability_registers + (mmio_read32(&capability_registers->rstoff) & ~0x1f));

    /* wait for the controller to be ready */
    int timeout = 3000;
    while (timeout != 0) {
        if (!(mmio_read32(&operational_registers->usbsts) & USBSTS_CNR)) {
            break;
        }
        hpet_sleep_ns(MS_TO_NS(1));
        timeout--;
    }

    if (unlikely(timeout == 0)) {
        klog("[xhci] xHCI controller timed out while waiting for controller to be ready\n");
        return;
    }

    /* walk controller capabilities and do bios handoff if needed */ 
    uint16_t xecp = (mmio_read32(&capability_registers->hccparams1) >> 16) & 0xffff;
    uint32_t* caps = (uint32_t*) ((uintptr_t) capability_registers + (xecp * 4));
    for (;;) {
        uint32_t cap = mmio_read32(caps);

        uint8_t capid = cap & 0xff;
        uint8_t next = (cap >> 8) & 0xff;

        switch (capid) {
            case 1:
                kpanic(NULL, false, "TODO: perform xHCI controller bios -> os handoff\n");
                break;
            case 2:
                uint32_t dword1 = mmio_read32(caps + 1);

                char proto[5];
                proto[0] = (dword1 >> 0) & 0xff;
                proto[1] = (dword1 >> 8) & 0xff;
                proto[2] = (dword1 >> 16) & 0xff;
                proto[3] = (dword1 >> 24) & 0xff;
                proto[4] = '\0';

                klog("[xhci] xHCI controller supports protocol %s v%d.%d\n",
                        proto, (cap >> 24) & 0xff, (cap >> 16) & 0xff);
                break;
        }

        if (next == 0) {
            break;
        }

        caps += next;
    }

    /* stop the controller */
    mmio_write32(&operational_registers->usbcmd, mmio_read32(&operational_registers->usbcmd) & ~USBCMD_RS);

    timeout = 2000;
    while (timeout != 0) {
        if (mmio_read32(&operational_registers->usbsts) & USBSTS_HCH) {
            break;
        }
        hpet_sleep_ns(MS_TO_NS(1));
        timeout--;
    }

    if (unlikely(timeout == 0)) {
        klog("[xhci] xHCI controller not responding to stop request\n");
        return;
    }

    /* reset the controller */
    mmio_write32(&operational_registers->usbcmd, mmio_read32(&operational_registers->usbcmd) | USBCMD_HCRST);

    timeout = 3000;
    while (timeout != 0) {
        if (!(mmio_read32(&operational_registers->usbcmd) & USBCMD_HCRST)) {
            break;
        }
        hpet_sleep_ns(MS_TO_NS(1));
        timeout--;
    }

    struct xhci_controller* controller = kmalloc(sizeof(struct xhci_controller));
    if (unlikely(controller == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for xHCI controller");
    }
    controller->capability_registers = capability_registers;
    controller->operational_registers = operational_registers;
    controller->runtime_registers = runtime_registers;
    controller->doorbell_registers = (void*) ((uintptr_t) capability_registers + mmio_read32(&capability_registers->dboff));

    uint32_t hcsparams1 = mmio_read32(&capability_registers->hcsparams1);
    uint8_t slot_count = hcsparams1 & 0xff;
    uint8_t port_count = (hcsparams1 >> 24) & 0xff;

    /* set controller max slot count */
    uint32_t config = mmio_read32(&operational_registers->config);
    config = (config & ~0xff) | slot_count;
    mmio_write32(&operational_registers->config, config);

    controller->slot_count = slot_count;
    controller->port_count = port_count;

    /* program device context base address array pointer and command ring pointer */
    uintptr_t dcbaa_paddr = pmm_alloc_zero(DIV_CEIL((slot_count + 1) * sizeof(uintptr_t), PAGE_SIZE_4KB));
    mmio_write64(&operational_registers->dcbaap, dcbaa_paddr);

    uint64_t* dcbaa = (void*) (dcbaa_paddr + HIGH_VMA);

    /* setup scratchpad registers */
    uint32_t hcsparams2 = mmio_read32(&capability_registers->hcsparams2);
    size_t scratchpad_count = (((hcsparams2 >> 21) & 0x1f) << 5) | ((hcsparams2 >> 27) & 0x1f);
    if (scratchpad_count > 0) {
        uintptr_t scratchpad_array_paddr = pmm_alloc_zero(DIV_CEIL(scratchpad_count * sizeof(uintptr_t), PAGE_SIZE_4KB));
        uintptr_t* scratchpad_array = (void*) (scratchpad_array_paddr + HIGH_VMA);

        for (size_t i = 0; i < scratchpad_count; i++) {
            scratchpad_array[i] = pmm_alloc_zero(1);
        }

        dcbaa[0] = scratchpad_array_paddr;
    } else {
        dcbaa[0] = 0;
    }

    uintptr_t cmd_ring_paddr = pmm_alloc_zero(DIV_CEIL(CMD_TRB_COUNT * sizeof(struct trb), PAGE_SIZE_4KB));

    struct xhci_ring* command_ring = &controller->command_ring;
    command_ring->trbs = (void*) (cmd_ring_paddr + HIGH_VMA);
    command_ring->cycle = true;

    struct trb* link = &command_ring->trbs[CMD_TRB_COUNT - 1];
    link->parameter = cmd_ring_paddr;
    link->status = 0;
    link->control = (6 << 10) | (1 << 1) | (command_ring->cycle ? 1 : 0);

    mmio_write64(&operational_registers->crcr, cmd_ring_paddr | (1 << 0));

    bool use_msix = false;

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for xHCI controller");
    }
    isr_register_handler(vector, xhci_irq_handler, controller);

    if (pci_enable_msix(pci_dev)) {
        use_msix = true;
        klog("[xhci] using MSI-X interrupts for xHCI controller\n");
    } else {
        klog("[xhci] using MSI interrupts for xHCI controller\n");
    }

    bool ret;
    if (use_msix) {
        ret = pci_setup_msix(pci_dev, 0, vector);
    } else {
        ret = pci_setup_msi(pci_dev, vector);
    }

    if (unlikely(!ret)) {
        klog("[xhci] failed to setup interrupts for xHCI controller");
        goto error;
    }

    struct interrupter_registers* interrupter_registers = &runtime_registers->interrupter_registers[0];
    mmio_write32(&interrupter_registers->iman, mmio_read32(&interrupter_registers->iman) | (1 << 1));

    /* setup interrupter event ring */
    uintptr_t event_ring_paddr = pmm_alloc_zero(DIV_CEIL(CMD_TRB_COUNT * sizeof(struct trb), PAGE_SIZE_4KB));
    uintptr_t erst_table_paddr = pmm_alloc_zero(DIV_CEIL(sizeof(struct event_ring_table_entry), PAGE_SIZE_4KB));

    struct xhci_ring* event_ring = &controller->event_ring;
    event_ring->trbs = (void*) (event_ring_paddr + HIGH_VMA);
    event_ring->cycle = true;

    struct event_ring_table_entry* erst_entry = (void*) (erst_table_paddr + HIGH_VMA);
    erst_entry->rsba = event_ring_paddr | 1;
    erst_entry->rsz = CMD_TRB_COUNT;

    mmio_write32(&interrupter_registers->erstsz, 1);
    mmio_write32(&interrupter_registers->erdp, event_ring_paddr | (1 << 3));
    mmio_write64(&interrupter_registers->erstba, erst_table_paddr | 1);

    mmio_write32(&operational_registers->usbsts, mmio_read32(&operational_registers->usbsts) | USBSTS_EINT);
    mmio_write32(&interrupter_registers->iman, mmio_read32(&interrupter_registers->iman) | (1 << 0));

    mmio_write32(&operational_registers->usbcmd, mmio_read32(&operational_registers->usbcmd) | USBCMD_INTE);

    if (use_msix) {
        pci_set_msix_mask(pci_dev, 0, false);
    } else {
        pci_set_msi_mask(pci_dev, false);
    }

    /* start the controller */
    mmio_write32(&operational_registers->usbcmd, mmio_read32(&operational_registers->usbcmd) | USBCMD_RS);
    while (mmio_read32(&operational_registers->usbsts) & USBSTS_HCH) {
        pause();
    }

    klog("[xhci] initialized xHCI controller\n");

    struct trb enable_slot = {0};
    enable_slot.trb_type = 9;

    klog("USBCMD=%08x USBSTS=%08x IMAN=%08x ERDP=%016lx\n",
            mmio_read32(&operational_registers->usbcmd),
            mmio_read32(&operational_registers->usbsts),
            interrupter_registers->iman,
            interrupter_registers->erdp);

    submit_cmd(controller, &enable_slot);

    klog("USBCMD=%08x USBSTS=%08x IMAN=%08x ERDP=%016lx\n",
            mmio_read32(&operational_registers->usbcmd),
            mmio_read32(&operational_registers->usbsts),
            interrupter_registers->iman,
            interrupter_registers->erdp);

    return;

error:
    isr_unregister_handler(vector);
    kfree(controller);
}

struct pci_driver xhci_driver = {
    .init = xhci_init,
    .name = "xhci",
    .match_condition = PCI_DRIVER_MATCH_ADDRESS,
    .match_data = {
        .class = 0x0c,
        .subclass = 0x03,
        .prog_if = 0x30,
    },
};
