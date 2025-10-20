#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/net/virtio_net.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <net/netif.h>
#include <net/packet.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define VIRTIO_NET_F_MTU    (1 << 3)
#define VIRTIO_NET_F_MAC    (1 << 5)

struct virtio_net_config {
    mac_address_t mac;
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
    uint32_t speed;
} __attribute__((packed));

struct virtio_net_header {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

struct virtio_net_device {
    struct virtio_device* dev;
    struct netif* netif;
};

static void virtio_net_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_net_device* net_dev = ctx;
    struct virtio_queue* rx_queue = &net_dev->dev->queues[0];

    spinlock_acquire(&rx_queue->lock);

    for (uint16_t i = rx_queue->last_used; i != rx_queue->used->index; i = (i + 1) % rx_queue->size) {
        volatile struct virtio_queue_descriptor* descriptor = &rx_queue->descriptors[i];

        const void* buf = (const void*) (descriptor->address + sizeof(struct virtio_net_header) + HIGH_VMA);
        size_t length = descriptor->length - sizeof(struct virtio_net_header);
        netif_add_packet(net_dev->netif, buf, length);

        virtio_queue_insert(rx_queue, i);
    }

    rx_queue->last_used = rx_queue->used->index;

    spinlock_release(&rx_queue->lock);
}

// TODO: figure out these two functions
static bool virtio_net_send_packet(struct netif* netif, struct packet* packet) {
    struct virtio_net_device* net_dev = netif->device;
    struct virtio_queue* tx_queue = &net_dev->dev->queues[1];

    spinlock_acquire(&tx_queue->lock);

    uint16_t desc = virtio_queue_alloc_descriptor(tx_queue);
    if (desc == 0xffff) {
        spinlock_release(&tx_queue->lock);
        return false;
    }

    volatile struct virtio_queue_descriptor* descriptor = &tx_queue->descriptors[desc];
    descriptor->address = pmm_alloc_zero(1);
    descriptor->length = packet->length + sizeof(struct virtio_net_header);
    descriptor->flags = 0;
    descriptor->next = 0;

    memcpy((void*) (descriptor->address + sizeof(struct virtio_net_header) + HIGH_VMA), packet->buf, packet->length);

    virtio_queue_insert(tx_queue, desc);

    spinlock_release(&tx_queue->lock);
    return true;
}

static void virtio_net_update_flags(struct netif* netif, uint16_t old_flags) {
    (void) netif;
    (void) old_flags;
}

void virtio_net_init(struct virtio_device* dev) {
    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_DRIVER);

    uint64_t features = VIRTIO_NET_F_MTU | VIRTIO_NET_F_MAC;
    if ((features = virtio_negotiate_features(dev, features)) == (uint64_t) -1) {
        klog("[virtio_net] failed to negotiate device features\n");
        return;
    }

    if (!(features & VIRTIO_NET_F_MAC)) {
        klog("[virtio_net] device does not have an assigned MAC address\n");
        return;
    }

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for VirtIO network device");
    }

    struct virtio_net_device* net_dev = kmalloc(sizeof(struct virtio_net_device));
    if (unlikely(net_dev == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO network device");
    }
    net_dev->dev = dev;

    struct netif* netif = netif_create();
    netif->device = net_dev;
    net_dev->netif = netif;

    volatile struct virtio_net_config* net_config = dev->device_config;

    netif->mtu = 1526;
    if (features & VIRTIO_NET_F_MTU) {
        netif->mtu = net_config->mtu;
    }

    memcpy(netif->mac, (void*) net_config->mac, sizeof(mac_address_t));
    netif->ipv4_address = IPV4_ADDRESS(192, 168, 100, 2);

    netif->send_packet = virtio_net_send_packet;
    netif->update_flags = virtio_net_update_flags;

    if (!virtio_queue_init(dev, 0, vector)) {
        klog("[virtio_net] failed to initialize VirtIO net device receive queue\n");
        return;
    }

    /* setup pakcet buffers in receieve queue */
    struct virtio_queue* rx_queue = &dev->queues[0];

    uintptr_t packet_buffer = pmm_alloc(DIV_CEIL(netif->mtu * rx_queue->size, PAGE_SIZE_4KB));

    for (uint16_t i = 0; i < rx_queue->size; i++) {
        volatile struct virtio_queue_descriptor* descriptor = &rx_queue->descriptors[i];
        descriptor->address = packet_buffer + (i * netif->mtu);
        descriptor->length = netif->mtu;
        descriptor->flags = VIRTQ_DESC_F_WRITE;
        virtio_queue_insert(rx_queue, i);
    }

    if (!virtio_queue_init(dev, 1, vector)) {
        klog("[virtio_net] failed to initialize VirtIO net device transmit queue\n");
        return;
    }

    isr_register_handler(vector, virtio_net_irq_handler, net_dev);

    klog("[virtio_net] initialized VirtIO network device (mac: " MAC_ADDRESS_FORMAT ")\n", MAC_ADDRESS_PRINT(netif->mac));

    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_DRIVER_OK);
}
