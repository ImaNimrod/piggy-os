#include <cpu/asm.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"

void device_try_init(struct xhci_controller* controller, uint8_t port) {
    struct trb enable_slot = {};
    enable_slot.trb_type = TRB_TYPE_ENABLE_SLOT;

    struct trb result;
    if (!ring_submit_and_wait(&controller->command_ring, &enable_slot, &result)) {
        return;
    }

    struct xhci_device* device = kmalloc(sizeof(struct xhci_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for xHCI device");
    }
    device->port_id = port;
    device->slot_id = (result.control >> 24) & 0xff;

    device->device_context_paddr = pmm_alloc_zero(1);
    mmio_write64(&controller->dcbaa[device->slot_id], device->device_context_paddr);

    ring_init(&device->ep_rings[0], &controller->doorbell_registers[device->slot_id], 1);

    klog("[xhci] 'initialized' device on port %u on HCI slot %u\n", port, device->slot_id);
    vector_push(controller->devices, device);
}
