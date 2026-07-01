#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/net/virtio_net.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <net/eth.h>
#include <net/netif.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/semaphore.h>
#include <utils/string.h>
#include <utils/wait_queue.h>

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
    uint16_t buffer_count;
} __attribute__((packed));

struct virtio_net_device {
    struct virtio_device* vio_dev;
    struct netif* netif;

    struct thread* rx_thread;
    struct wait_queue rx_wq;

    struct thread** tx_queue_waiters;
    semaphore_t tx_semaphore;
};

static void rx_handler(void* arg) {
    struct virtio_net_device* device = arg;
    struct virtio_queue* rx_queue = &device->vio_dev->queues[0];

    for (;;) {
        wait_queue_wait(&device->rx_wq);

        bool int_state = spinlock_acquire_irqsave(&rx_queue->lock);

        while (rx_queue->last_used != rx_queue->used->index) {
            uint16_t index = rx_queue->last_used++ % rx_queue->size;
            uint16_t desc = rx_queue->used->ring[index].id;

            struct virtio_queue_descriptor* descriptor = &rx_queue->descriptors[desc];

            const void* buf = (const void*) (descriptor->address + sizeof(struct virtio_net_header) + HIGH_VMA);
            eth_handle(device->netif, buf);

            virtio_queue_insert(rx_queue, desc);
        }

        virtio_queue_notify(rx_queue);

        spinlock_release_irqsave(&rx_queue->lock, int_state);
    }
}

static void virtio_net_rx_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_net_device* device = ctx;
    wait_queue_wake_one(&device->rx_wq);
}

static void virtio_net_tx_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_net_device* device = ctx;
    struct virtio_queue* tx_queue = &device->vio_dev->queues[1];

    bool int_state = spinlock_acquire_irqsave(&tx_queue->lock);

    while (tx_queue->last_used != tx_queue->used->index) {
        uint16_t index = tx_queue->last_used++ % tx_queue->size;
        uint16_t desc = tx_queue->used->ring[index].id;

        pmm_free(tx_queue->descriptors[desc].address, 1);
        tx_queue->descriptors[desc].address = 0;

        scheduler_wakeup(device->tx_queue_waiters[desc], 0);
        device->tx_queue_waiters[desc] = NULL;

        semaphore_signal(&device->tx_semaphore);

        virtio_queue_free_descriptor(tx_queue, desc);
    }

    spinlock_release_irqsave(&tx_queue->lock, int_state);
}

static bool virtio_net_alloc_packet(struct netif* netif, struct packet* packet, size_t size) {
    uintptr_t paddr = pmm_alloc_zero(1);
    packet->buf = (void*) (paddr + HIGH_VMA);
    packet->size = sizeof(struct virtio_net_header) + sizeof(struct eth_header) + size;
    packet->offset = sizeof(struct virtio_net_header) + sizeof(struct eth_header);
    return true;
}

static void virtio_net_free_packet(struct netif* netif, struct packet* packet) {
    pmm_free((uintptr_t) packet->buf - HIGH_VMA, 1);
}

static bool virtio_net_send_packet(struct netif* netif, struct packet* packet, mac_address_t* destination, ethertype_t type) {
    struct virtio_net_device* device = netif->device;

    semaphore_wait(&device->tx_semaphore);

    struct virtio_queue* tx_queue = &device->vio_dev->queues[1];

    bool int_state = spinlock_acquire_irqsave(&tx_queue->lock);

    uint16_t desc = virtio_queue_alloc_descriptor(tx_queue);
    if (desc == VIRTIO_INVALID_QUEUE_DESCRIPTOR) {
        spinlock_release_irqsave(&tx_queue->lock, int_state);
        return false;
    }

    memset(packet->buf, 0, sizeof(struct virtio_net_header));
    //struct virtio_net_header* hdr = packet->buf;
    //hdr->hdr_len = sizeof(struct virtio_net_header) + sizeof(struct eth_header);

    eth_populate_packet(netif, destination, type, (void*) ((uintptr_t) packet->buf + sizeof(struct virtio_net_header)));

    struct virtio_queue_descriptor* descriptor = &tx_queue->descriptors[desc];
    descriptor->address = pmm_alloc(1);
    descriptor->length = packet->size;
    descriptor->flags = 0;

    memcpy((void*) (descriptor->address +  HIGH_VMA), packet->buf, packet->size);

    device->tx_queue_waiters[desc] = this_cpu()->scheduler.current_thread;
    virtio_queue_insert(tx_queue, desc);

    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);

    spinlock_release(&tx_queue->lock);

    virtio_queue_notify(tx_queue);

    scheduler_yield();

    return true;
}

static void virtio_net_update_flags(struct netif* netif, uint16_t old_flags) {
    (void) netif;
    (void) old_flags;
}

void virtio_net_init(struct virtio_device* vio_dev) {
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER);

    uint64_t features = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC;
    if ((features = virtio_negotiate_features(vio_dev, features)) == (uint64_t) -1) {
        klog("[virtio_net] failed to negotiate device features\n");
        return;
    }

    if (!(features & VIRTIO_NET_F_MAC)) {
        klog("[virtio_net] device does not have an assigned MAC address\n");
        return;
    }

    struct virtio_net_device* device = kmalloc(sizeof(struct virtio_net_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO network device");
    }
    device->vio_dev = vio_dev;

    struct netif* netif = kmalloc(sizeof(struct netif));
    if (unlikely(!netif)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO network interface");
    }
    netif->mtu = 1514;
    netif->device = device;
    netif->alloc_packet = virtio_net_alloc_packet;
    netif->free_packet = virtio_net_free_packet;
    netif->send_packet = virtio_net_send_packet;
    netif->update_flags = virtio_net_update_flags;

    device->netif = netif;

    struct virtio_net_config* net_config = vio_dev->device_config;

    memcpy(netif->mac, (void*) net_config->mac, sizeof(mac_address_t));
    netif->ipv4_address = IPV4_ADDRESS(192, 168, 100, 2);

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for VirtIO network device RX queue");
    }

    if (!virtio_queue_init(vio_dev, 0, vector)) {
        klog("[virtio_net] failed to initialize VirtIO network device RX queue\n");
        return;
    }

    isr_register_handler(vector, virtio_net_rx_irq_handler, device);

    // Setup packet buffers in receieve queue
    struct virtio_queue* rx_queue = &vio_dev->queues[0];

    size_t rx_buf_size = sizeof(struct virtio_net_header) + netif->mtu;
    uintptr_t packet_buffer = pmm_alloc(DIV_CEIL(rx_buf_size * rx_queue->size, PAGE_SIZE_4KB));

    for (uint16_t i = 0; i < rx_queue->size; i++) {
        struct virtio_queue_descriptor* descriptor = &rx_queue->descriptors[i];
        descriptor->address = packet_buffer + (i * rx_buf_size);
        descriptor->length = rx_buf_size;
        descriptor->flags = VIRTQ_DESC_F_WRITE;
        virtio_queue_insert(rx_queue, i);
    }

    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for VirtIO network device TX queue");
    }

    if (!virtio_queue_init(vio_dev, 1, vector)) {
        klog("[virtio_net] failed to initialize VirtIO net device TX queue\n");
        return;
    }

    device->rx_thread = thread_create_kernel((uintptr_t) rx_handler, device);
    if (unlikely(!device->rx_thread)) {
        kpanic(NULL, false, "failed to create network receive worker thread");
    }
    wait_queue_init(&device->rx_wq);

    device->tx_queue_waiters = kmalloc(sizeof(struct thread*) * vio_dev->queues[1].size);
    if (unlikely(!device->tx_queue_waiters)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO net TX queue waiters");
    }
    semaphore_init(&device->tx_semaphore, vio_dev->queues[1].size);

    isr_register_handler(vector, virtio_net_tx_irq_handler, device);
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER_OK);

    klog("[virtio_net] initialized VirtIO network device (mac: " MAC_ADDRESS_FORMAT ")\n", MAC_ADDRESS_PRINT(netif->mac));
}
