#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/block/block.h>
#include <dev/block/virtio_blk.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/semaphore.h>
#include <utils/spinlock.h>

#include "../../utils/printf/printf.h"

#define VIRTIO_BLK_F_RO     (1 << 5)
#define VIRTIO_BLK_F_FLUSH  (1 << 9)

#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1
#define VIRTIO_BLK_T_FLUSH  4

struct virtio_blk_config {
    uint64_t capacity;
} __attribute__((packed));

struct virtio_blk_request {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_device {
    struct virtio_device* vio_dev;
    uint64_t features;

    struct thread** queue_waiters; 
    semaphore_t queue_semaphore;
};

static dev_t virtio_blk_device_minor;

static ssize_t virtio_blk_cmd_handler(struct block_device* block_device, block_cmd_t cmd, uint64_t lba, size_t block_count, uintptr_t paddr) {
    struct virtio_blk_device* device = block_device->private;

    if (cmd == CMD_WRITE && device->features & VIRTIO_BLK_F_RO) {
        return -EIO;
    }

    semaphore_wait(&device->queue_semaphore);

    struct virtio_queue* queue = &device->vio_dev->queues[0];

    spinlock_acquire(&queue->lock);

    uint16_t desc0 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc1 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc2 = virtio_queue_alloc_descriptor(queue);

    uintptr_t request_paddr = pmm_alloc(1);

    struct virtio_blk_request* request = (void*) (request_paddr + HIGH_VMA);
    request->type = cmd == CMD_READ ? VIRTIO_BLK_T_IN : VIRTIO_BLK_T_OUT;
    request->reserved = 0;
    request->sector = lba;

    queue->descriptors[desc0].address = request_paddr;
    queue->descriptors[desc0].length = 16;
    queue->descriptors[desc0].flags = VIRTQ_DESC_F_NEXT;
    queue->descriptors[desc0].next = desc1;

    queue->descriptors[desc1].address = paddr;
    queue->descriptors[desc1].length = block_count * block_device->block_size;
    queue->descriptors[desc1].flags = VIRTQ_DESC_F_NEXT;
    if (cmd == CMD_READ) {
        queue->descriptors[desc1].flags |= VIRTQ_DESC_F_WRITE;
    }
    queue->descriptors[desc1].next = desc2;

    queue->descriptors[desc2].address = request_paddr + sizeof(struct virtio_blk_request);
    queue->descriptors[desc2].length = 1;
    queue->descriptors[desc2].flags = VIRTQ_DESC_F_WRITE;
    queue->descriptors[desc2].next = 0;

    uint16_t index = virtio_queue_insert(queue, desc0);
    device->queue_waiters[index] = this_cpu()->running_thread;

    spinlock_release(&queue->lock);

    virtio_queue_notify(queue);

    scheduler_block(this_cpu()->running_thread);

    uint8_t status = *(uint8_t*) (request_paddr + HIGH_VMA + sizeof(struct virtio_blk_request));
    if (status != VIRTIO_BLK_S_OK) {
        return -EIO;
    }

    pmm_free(request_paddr, 1);
    return block_count;
}

static void virtio_blk_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_blk_device* device = ctx;
    struct virtio_queue* queue = &device->vio_dev->queues[0];

    bool int_state = spinlock_acquire_irqsave(&queue->lock);

    while (queue->last_used != queue->used->index) {
        uint16_t i = queue->last_used++ % queue->size;
        uint16_t desc_id = queue->used->ring[i].id;

        scheduler_unblock(device->queue_waiters[i]);
        device->queue_waiters[i] = NULL;
        semaphore_signal(&device->queue_semaphore);

        virtio_queue_free_descriptor(queue, desc_id);
    }

    spinlock_release_irqsave(&queue->lock, int_state);
}

void virtio_blk_init(struct virtio_device* vio_dev) {
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER);

    uint64_t features = VIRTIO_BLK_F_RO | VIRTIO_BLK_F_FLUSH;
    if ((features = virtio_negotiate_features(vio_dev, features)) == (uint64_t) -1) {
        klog("[virtio_blk] failed to negotiate device features\n");
        return;
    }

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for VirtIO block device");
    }

    if (!virtio_queue_init(vio_dev, 0, vector)) {
        klog("[virtio_blk] failed to initialize VirtIO block device request queue\n");
        return;
    }

    size_t sector_count = mmio_read64(&(((struct virtio_blk_config*) vio_dev->device_config)->capacity));
    size_t sector_size = 512;

    size_t total_size;
    if (__builtin_mul_overflow(sector_count, sector_size, &total_size)) {
        kpanic(NULL, false, "VirtIO block device disk size overflow");
    }

    struct virtio_blk_device* device = kmalloc(sizeof(struct virtio_blk_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO block device\n");
    }
    device->vio_dev = vio_dev;
    device->features = features;

    device->queue_waiters = kmalloc(sizeof(struct thread*) * vio_dev->queues[0].size);
    if (unlikely(device->queue_waiters == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO block device queued threads");
    }

    semaphore_init(&device->queue_semaphore, vio_dev->queues[0].size);

    isr_register_handler(vector, virtio_blk_irq_handler, device);
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER_OK);

    klog("[virtio_blk] initialized VirtIO block device (size: %zuGB, block size: %zuB)\n", total_size / 1000000000, sector_size);

    char name[10];
    snprintf(name, sizeof(name) - 1, "vioblk%zu", virtio_blk_device_minor);

    struct block_device block_device = {
        .cmd_handler = virtio_blk_cmd_handler,
        .private = device,
        .block_count = sector_count,
        .block_size = sector_size,
        .lba_offset = 0,
    };

    int ret = block_register(name, makedev(VIOBLK_DEV_MAJOR, virtio_blk_device_minor), &block_device, true);
    if (ret < 0) {
        kfree(device);
        return;
    }

    virtio_blk_device_minor++;
}
