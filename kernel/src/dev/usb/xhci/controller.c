#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/usb/xhci.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#include "definitions.h"

// TODO: support multiple interrupters or maybe just commit sepuku instead of doing USB :)
// TODO: for some intel controllers, switch ports from EHCI to XHCI

static bool reset_port(struct xhci_controller* controller, uint8_t port) {
    struct port_registers* port_registers = &controller->operational_registers->port_registers[port];

    int timeout;

    uint32_t portsc = mmio_read32(&port_registers->portsc);
    if (!(portsc & PORTSC_PP)) {
        portsc |= PORTSC_PP;
        mmio_write32(&port_registers->portsc, portsc);

        timeout = 50;
        while (timeout != 0) {
            if (mmio_read32(&port_registers->portsc) & PORTSC_PP) {
                break;
            }
            timer_wait_ns(MS_TO_NS(1));
            timeout--;
        }

        if (unlikely(timeout == 0)) {
            klog("[xhci] timed out while waiting for port %u to be powered\n", port);
            return false;
        }
    }

    struct port_protocol_info* info = NULL;
    for (size_t i = 0; i < vector_size(controller->port_protocol_info); i++) {
        struct port_protocol_info* iter = (void*) vector_get(controller->port_protocol_info, i);
        if (iter->port_start <= port && iter->port_end >= port) {
            info = iter;
            break;
        }
    }

    if (unlikely(info == NULL)) {
        return false;
    }

    bool is_usb3 = info->version_major == 3;
    if (is_usb3) {
        portsc |= PORTSC_WPR;
    } else {
        portsc |= PORTSC_PR;
    }

    mmio_write32(&port_registers->portsc, portsc);

    uint32_t success_bit = is_usb3 ? PORTSC_WPR : PORTSC_PED;

    timeout = 50;
    while (timeout != 0) {
        if (mmio_read32(&port_registers->portsc) & success_bit) {
            break;
        }
        timer_wait_ns(MS_TO_NS(1));
        timeout--;
    }

    if (unlikely(timeout == 0)) {
        klog("[xhci] timed out while waiting for port %u to reset\n", port);
        return false;
    }

    return true;
}

static void xhci_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct xhci_controller* controller = arg;

    struct operational_registers* operational_registers = controller->operational_registers;
    if (!(mmio_read32(&operational_registers->usbsts) & USBSTS_EINT)) {
        return;
    }

    struct interrupter_registers* interrupter_registers = &controller->runtime_registers->interrupter_registers[0];

    struct trb* trb;
    while ((trb = ring_dequeue(&controller->event_ring)) != NULL) {
        uint32_t trb_type = trb->trb_type;

        if (trb_type == TRB_TYPE_TRANSFER_EVENT || trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
            struct xhci_ring* ring;
            if (trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
                ring = &controller->command_ring;
            } else {
                uint8_t slot_id = (trb->control >> 24) & 0xff;
                struct xhci_device* device = *vector_get(controller->devices, slot_id);
                ring = &device->ep_rings[(trb->control >> 16) & 0x1f];
            }

            bool int_save = spinlock_acquire_irqsave(&ring->lock);

            bool found_waiter = false;
            struct completion_waiter waiter;

            for (size_t i = 0; i < vector_size(ring->completion_waiters); i++) {
                struct completion_waiter* iter = (void*) vector_get(ring->completion_waiters, i);
                if (iter->submission_trb_paddr == trb->parameter) {
                    found_waiter = true;
                    memcpy(&waiter, iter, sizeof(struct completion_waiter));
                    vector_remove(ring->completion_waiters, i);
                    break;
                }
            }

            if (found_waiter) {
                memcpy(waiter.completion_trb, trb, sizeof(struct trb));
                scheduler_wakeup(waiter.thread, 0);
            }

            spinlock_release_irqsave(&ring->lock, int_save);
        } else if (trb_type == TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
            klog("[xhci] port status change\n");
        } else {
            klog("[xhci] unknown event TRB type: %u\n", trb_type);
        }

        uintptr_t event_ring_paddr = controller->event_ring.trb_paddr;
        event_ring_paddr += controller->event_ring.index * sizeof(struct trb);
        mmio_write64(&interrupter_registers->erdp, event_ring_paddr | (1 << 3));
    }

    mmio_write32(&interrupter_registers->iman, mmio_read32(&interrupter_registers->iman) | IMAN_IP);
    mmio_write32(&operational_registers->usbsts, mmio_read32(&operational_registers->usbsts) | USBSTS_EINT);
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
        timer_wait_ns(MS_TO_NS(1));
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

        if (capid == 1) {
            if (!(cap & USB_LEGACY_BOS)) {
                break;
            }

            mmio_write32(caps, cap | USB_LEGACY_OOS);

            timeout = 1000;
            while (timeout != 0) {
                if (mmio_read32(&caps) & USB_LEGACY_OOS) {
                    break;
                }
                timer_wait_ns(MS_TO_NS(1));
                timeout--;
            }

            if (unlikely(timeout == 0)) {
                klog("[xhci] xHCI controller timed out while waiting for BIOS to OS handoff\n");
                return;
            }
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
        timer_wait_ns(MS_TO_NS(1));
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
        timer_wait_ns(MS_TO_NS(1));
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

    controller->port_protocol_info = vector_create(sizeof(struct port_protocol_info));
    if (unlikely(controller->port_protocol_info == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for xHCI controller port info");
    }
    controller->devices = vector_create(sizeof(struct xhci_device*));
    if (unlikely(controller->devices == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for xHCI devices");
    }

    uint32_t hcsparams1 = mmio_read32(&capability_registers->hcsparams1);
    uint8_t slot_count = hcsparams1 & 0xff;
    uint8_t port_count = (hcsparams1 >> 24) & 0xff;

    /* set controller max slot count */
    uint32_t config = mmio_read32(&operational_registers->config);
    config = (config & 0xffffff00) | slot_count;
    mmio_write32(&operational_registers->config, config);

    controller->slot_count = slot_count;
    controller->port_count = port_count;

    caps = (uint32_t*) ((uintptr_t) capability_registers + (xecp * 4));
    for (;;) {
        uint32_t cap = mmio_read32(caps);

        uint8_t capid = cap & 0xff;
        uint8_t next = (cap >> 8) & 0xff;

        if (capid == 2) {
            uint32_t dword1 = mmio_read32(caps + 1);
            if (dword1 != 0x20425355) {
                break;
            }

            uint8_t version_major = (cap >> 24) & 0xff;
            uint8_t version_minor = (cap >> 16) & 0xff;

            uint32_t dword2 = mmio_read32(caps + 2);
            uint8_t start = (dword2 & 0xff) - 1;
            uint8_t count = (dword2 >> 8) & 0xff;

            klog("[xhci] xHCI controller supports protocol USB v%d.%d on ports %u-%u\n",
                    version_major, version_minor, start, start + count - 1);

            struct port_protocol_info info = {
                .port_start = start,
                .port_end = start + count - 1,
                .version_major = version_major,
                .version_minor = version_minor,
            };

            vector_push(controller->port_protocol_info, &info);
        }

        if (next == 0) {
            break;
        }

        caps += next;
    }

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for xHCI controller");
    }
    isr_register_handler(vector, xhci_irq_handler, controller);

    bool use_msix = false;

    if (pci_enable_msix(pci_dev)) {
        pci_setup_msix(pci_dev, 0, vector);
        use_msix = true;
    } else {
        if (!pci_setup_msi(pci_dev, vector)) {
            klog("[xhci] failed to enable PCI interrupts for xHCI controller\n");
            goto error;
        }
    }

    /* program device context base address array pointer and setup scratchpad registers */
    uintptr_t dcbaa_paddr = pmm_alloc_zero(DIV_CEIL((slot_count + 1) * sizeof(uintptr_t), PAGE_SIZE_4KB));
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
    }

    controller->dcbaa = dcbaa;
    mmio_write64(&operational_registers->dcbaap, dcbaa_paddr);

    /* program command ring */
    ring_init(&controller->command_ring, &controller->doorbell_registers[0], 0);
    mmio_write64(&operational_registers->crcr, controller->command_ring.trb_paddr | (1 << 0));

    /* setup interrupter event ring */
    ring_init(&controller->event_ring, NULL, 0);

    uintptr_t erst_table_paddr = pmm_alloc(1);

    struct event_ring_table_entry* erst_entry = (void*) (erst_table_paddr + HIGH_VMA);
    erst_entry->rsba = controller->event_ring.trb_paddr;
    erst_entry->rsz = controller->event_ring.size;
    erst_entry->reserved = 0;

    struct interrupter_registers* interrupter_registers = &runtime_registers->interrupter_registers[0];
    mmio_write32(&interrupter_registers->erstsz, 1);
    mmio_write64(&interrupter_registers->erstba, erst_table_paddr);
    mmio_write32(&interrupter_registers->erdp, controller->event_ring.trb_paddr | (1 << 3));

    /* renable interrupts */
    mmio_write32(&interrupter_registers->imod, 0);
    mmio_write32(&interrupter_registers->iman, IMAN_IE);

    mmio_write32(&operational_registers->usbsts, mmio_read32(&operational_registers->usbsts) | USBSTS_EINT);
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

    /* initialize all connected devices */
    for (uint8_t i = 0; i < port_count; i++) {
        struct port_registers* port_registers = &operational_registers->port_registers[i];

        uint32_t portsc = mmio_read32(&port_registers->portsc);
        if (!(portsc & PORTSC_CCS)) {
            continue;
        }

        if (reset_port(controller, i)) {
            device_try_init(controller, i);
        }
    }

    klog("[xhci] initialized xHCI controller\n");
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
