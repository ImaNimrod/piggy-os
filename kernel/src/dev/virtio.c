#include <cpu/asm.h>
#include <dev/block/virtio_blk.h>
#include <dev/net/virtio_net.h>
#include <dev/virtio.h>
#include <mem/paging.h> 
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VIRTIO_TYPE_NET     1
#define VIRTIO_TYPE_BLOCK   2

uint64_t virtio_negotiate_features(struct virtio_device* dev, uint64_t features) {
    mmio_write32(&dev->common_config->device_feature_select, 0);
    uint64_t available = mmio_read32(&dev->common_config->device_feature);
    mmio_write32(&dev->common_config->device_feature_select, 1);
    available |= (uint64_t) mmio_read32(&dev->common_config->device_feature) << 32;

    uint64_t negotiable = features & available;

    mmio_write32(&dev->common_config->driver_feature_select, 0);
    mmio_write32(&dev->common_config->driver_feature, (uint32_t) negotiable);
    mmio_write32(&dev->common_config->driver_feature_select, 1);
    mmio_write32(&dev->common_config->driver_feature, (uint32_t) (negotiable >> 32));

    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_FEATURES_OK);
    if (!(mmio_read8(&dev->common_config->status) & VIRTIO_STATUS_FEATURES_OK)) {
        return (uint64_t) -1;
    }

    return negotiable;
}

uint16_t virtio_queue_alloc_descriptor(struct virtio_queue* queue) {
    for (uint16_t i = 0; i < queue->size; i++) {
        if (queue->descriptors[i].address == 0) {
            queue->descriptors[i].address = 0xffffffff;
            return i;
        }
    }

    return VIRTIO_INVALID_QUEUE_DESCRIPTOR;
}

void virtio_queue_free_descriptor(struct virtio_queue* queue, uint16_t descriptor) {
    for (;;) {
        queue->descriptors[descriptor].address = 0;

        if (!(queue->descriptors[descriptor].flags & VIRTQ_DESC_F_NEXT)) {
            return;
        }

        descriptor = queue->descriptors[descriptor].next;
    }
}

bool virtio_queue_init(struct virtio_device* dev, uint16_t queue_number, uint8_t irq_vector) {
    if (queue_number > mmio_read16(&dev->common_config->queue_count)) {
        return false;
    }

    mmio_write16(&dev->common_config->queue_select, queue_number);

    struct virtio_queue* queue = &dev->queues[queue_number];
    queue->size = mmio_read16(&dev->common_config->queue_size);

    spinlock_init(&queue->lock);

    size_t total_size = ALIGN_UP(sizeof(struct virtio_queue_descriptor) * queue->size, PAGE_SIZE_4KB) +         // Available ring needs to be 4096-byte aligned 
        ALIGN_UP((sizeof(struct virtio_queue_available) + (sizeof(uint16_t) * queue->size)), PAGE_SIZE_4KB) +   // Used ring needs to be 4096-byte aligned
        (sizeof(struct virtio_queue_used) + (sizeof(struct virtio_queue_used_entry) * queue->size));

    uintptr_t queue_paddr = pmm_alloc_zero(DIV_CEIL(total_size, PAGE_SIZE_4KB));

    uintptr_t descriptor_paddr = queue_paddr;
    queue->descriptors = (void*) (descriptor_paddr + HIGH_VMA);
    uintptr_t available_paddr = ALIGN_UP(descriptor_paddr + (sizeof(struct virtio_queue_descriptor) + queue->size), PAGE_SIZE_4KB);
    queue->available = (void*) (available_paddr + HIGH_VMA);
    uintptr_t used_paddr = ALIGN_UP(available_paddr + (sizeof(struct virtio_queue_available) + (sizeof(uint16_t) * queue->size)), PAGE_SIZE_4KB);
    queue->used = (void*) (used_paddr + HIGH_VMA);

    mmio_write64(&dev->common_config->queue_desc, descriptor_paddr);
    mmio_write64(&dev->common_config->queue_driver, available_paddr);
    mmio_write64(&dev->common_config->queue_device, used_paddr);

    queue->notify = dev->notify_begin + (mmio_read16(&dev->common_config->queue_notify_offset) * dev->notify_offset_multiplier);

    if (irq_vector != 0xff) {
        mmio_write16(&dev->common_config->queue_msix_vector, queue_number);

        if (!pci_setup_msix(dev->pci_dev, queue_number, irq_vector)) {
            return false;
        }
        pci_set_msix_mask(dev->pci_dev, queue_number, false);
    }

    mmio_write16(&dev->common_config->queue_enable, 1);
    return true;
}

uint16_t virtio_queue_insert(struct virtio_queue* queue, uint16_t descriptor) {
    queue->available->ring[queue->available->index % queue->size] = descriptor;
    return queue->available->index++;
}

void virtio_queue_notify(struct virtio_queue* queue) {
    mmio_write32(queue->notify, 0);
}

static void virtio_init(struct pci_device* pci_dev) {
    if (pci_dev->device_id < 0x1000 || pci_dev->device_id > 0x103f) {
        return;
    }

    klog("[virtio] found VirtIO device [%04x:%04x]\n", pci_dev->vendor_id, pci_dev->device_id);

    struct virtio_device* dev = kmalloc(sizeof(struct virtio_device));
    if (unlikely(dev == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO device");
    }
    dev->pci_dev = pci_dev;

    uint8_t mmio_bar = 0;
    uint32_t common_config_offset = 0;
    uint32_t device_config_offset = 0;
    uint32_t notify_offset = 0;

    uint8_t next_offset = pci_read(pci_dev, PCI_CONFIG_CAPABILITIES, 1);
    while (next_offset != 0) {
        if (pci_read(pci_dev, next_offset, 1) == 0x09) {
            switch (pci_read(pci_dev, next_offset + 3, 1)) {
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    mmio_bar = pci_read(pci_dev, next_offset + 4, 1);
                    common_config_offset = pci_read(pci_dev, next_offset + 8, 4);
                    break;
                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    notify_offset = pci_read(pci_dev, next_offset + 8, 4);
                    dev->notify_offset_multiplier = pci_read(pci_dev, next_offset + 16, 4);
                    break;
                case VIRTIO_PCI_CAP_DEVICE_CFG:
                    device_config_offset = pci_read(pci_dev, next_offset + 8, 4);
                    break;
            }
        }

        next_offset = pci_read(pci_dev, next_offset + 1, 1);
    }

    struct pci_bar bar;
    if (!pci_get_bar(pci_dev, mmio_bar, &bar)) {
        klog("[virtio] unable to get PCI BAR for VirtIO device MMIO access\n");
        kfree(dev);
        return;
    }
    if (!pci_map_bar(&bar)) {
        klog("[virtio] failed to map memory for VirtIO device MMIO access\n");
        kfree(dev);
        return;
    }

    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER | PCI_COMMAND_FLAG_INTX_DISABLE, true);
    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_IO_SPACE, false);

    dev->common_config = (void*) (bar.base_address + HIGH_VMA + common_config_offset);
    dev->device_config = (void*) (bar.base_address + HIGH_VMA + device_config_offset);
    dev->notify_begin = (void*) (bar.base_address + HIGH_VMA + notify_offset);

    if (!pci_enable_msix(pci_dev)) {
        klog("[virtio] failed to setup interrupts for VirtIO device\n");
        kfree(dev);
        return;
    }

    struct virtio_queue* queues = kmalloc(sizeof(struct virtio_queue) * mmio_read16(&dev->common_config->queue_count));
    if (unlikely(queues == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO device queues");
    }
    dev->queues = queues;

    // First, reset the device
    mmio_write8(&dev->common_config->status, 0);
    while (mmio_read8(&dev->common_config->status) != 0) {
        pause();
    }

    // Then, acknowlege the device
    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_ACKNOWLEDGE);

    switch (pci_read_subsystem_id(pci_dev)) {
        case VIRTIO_TYPE_NET:
            virtio_net_init(dev);
            break;
        case VIRTIO_TYPE_BLOCK:
            virtio_blk_init(dev);
            break;
    }
}

struct pci_driver virtio_driver = {
    .init = virtio_init,
    .name = "virtio",
    .match_condition = PCI_DRIVER_MATCH_VENDOR_ID,
    .match_data = {
        .vendor_id = 0x1af4,
    },
};
