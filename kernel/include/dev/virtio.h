#ifndef _KERNEL_DEV_VIRTIO_H
#define _KERNEL_DEV_VIRTIO_H

#include <dev/pci.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

#define VIRTIO_INVALID_QUEUE_DESCRIPTOR     ((uint16_t) 0xffff)

#define VIRTIO_STATUS_ACKNOWLEDGE           (1 << 0)
#define VIRTIO_STATUS_DRIVER                (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK             (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK           (1 << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET    (1 << 6)
#define VIRTIO_STATUS_FAILED                (1 << 7)

#define VIRTQ_DESC_F_NEXT       (1 << 0)
#define VIRTQ_DESC_F_WRITE      (1 << 1)
#define VIRTQ_DESC_F_INDIRECT   (1 << 2)

#define VIRTQ_AVAIL_F_NO_INTERRUPT (1 << 0)

#define VIRTQ_USED_F_NO_NOTIFY (1 << 0)

struct virtio_common_config {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_vector;
    uint16_t queue_count;
    uint8_t status;
    uint8_t generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_offset;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
    uint16_t queue_notify_data;
    uint16_t queue_reset;
} __attribute__((packed));

struct virtio_queue_descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct virtio_queue_available {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[];
};

struct virtio_queue_used_entry {
    uint32_t id;
    uint32_t length;
};

struct virtio_queue_used {
    uint16_t flags;
    uint16_t index;
    struct virtio_queue_used_entry ring[];
};

struct virtio_queue {
    uint16_t size;
    uint16_t last_used;
    struct virtio_queue_descriptor* descriptors;
    struct virtio_queue_available* available;
    struct virtio_queue_used* used;
    uint32_t* notify;
    spinlock_t lock;
};

struct virtio_device {
    struct pci_device* pci_dev;

    struct virtio_common_config* common_config;
    void* device_config;

    uint32_t* notify_begin;
    uint32_t notify_offset_multiplier;

    struct virtio_queue* queues;
};

uint64_t virtio_negotiate_features(struct virtio_device* dev, uint64_t features);

uint16_t virtio_queue_alloc_descriptor(struct virtio_queue* queue);
void virtio_queue_free_descriptor(struct virtio_queue* queue, uint16_t descriptor);
bool virtio_queue_init(struct virtio_device* dev, uint16_t queue_number, uint8_t irq_vector);
uint16_t virtio_queue_insert(struct virtio_queue* queue, uint16_t descriptor);
void virtio_queue_notify(struct virtio_queue* queue);

extern struct pci_driver virtio_driver;

#endif /* _KERNEL_DEV_VIRTIO_H */
