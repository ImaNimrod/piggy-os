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

#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)
#define VIRTIO_BLK_F_SEGMENT_MAX    (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY_BIT   (1 << 4)
#define VIRTIO_BLK_F_RO             (1 << 5)
#define VIRTIO_BLK_F_BLOCK_SIZE     (1 << 6)
#define VIRTIO_BLK_F_FLUSH          (1 << 9)
#define VIRTIO_BLK_F_TOPOLOGY       (1 << 10)

#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1
#define VIRTIO_BLK_T_FLUSH  4

struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;

    struct virtio_blk_geometry {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;

    uint32_t block_size;

    struct virtio_blk_topology {
        uint8_t physical_block_exp;
        uint8_t alignment_offset;
        uint16_t minimal_io_size;
        uint32_t optimal_io_size;
    } topology;
} __attribute__((packed));

struct virtio_blk_request {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_device {
    struct virtio_device* vio_dev;
    uint64_t features;
    size_t io_size_blocks;

    struct thread** queue_waiters; 
    semaphore_t queue_semaphore;
};

static dev_t virtio_blk_device_minor;

static int virtio_blk_rw(struct virtio_blk_device* device, uint64_t lba, size_t size, uintptr_t paddr, bool write) {
    semaphore_wait(&device->queue_semaphore);

    struct virtio_queue* queue = &device->vio_dev->queues[0];
    spinlock_acquire(&queue->lock);

    uint16_t desc0 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc1 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc2 = virtio_queue_alloc_descriptor(queue);

    uintptr_t request_paddr = pmm_alloc(1);

    struct virtio_blk_request* request = (void*) (request_paddr + HIGH_VMA);
    request->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    request->reserved = 0;
    request->sector = lba;

    queue->descriptors[desc0].address = request_paddr;
    queue->descriptors[desc0].length = sizeof(struct virtio_blk_request);
    queue->descriptors[desc0].flags = VIRTQ_DESC_F_NEXT;
    queue->descriptors[desc0].next = desc1;

    queue->descriptors[desc1].address = paddr;
    queue->descriptors[desc1].length = size;
    queue->descriptors[desc1].flags = VIRTQ_DESC_F_NEXT;
    if (!write) {
        queue->descriptors[desc1].flags |= VIRTQ_DESC_F_WRITE;
    }
    queue->descriptors[desc1].next = desc2;

    queue->descriptors[desc2].address = request_paddr + sizeof(struct virtio_blk_request);
    queue->descriptors[desc2].length = 1;
    queue->descriptors[desc2].flags = VIRTQ_DESC_F_WRITE;
    queue->descriptors[desc2].next = 0;

    device->queue_waiters[desc0] = this_cpu()->scheduler.current_thread;
    virtio_queue_insert(queue, desc0);

    spinlock_release(&queue->lock);

    virtio_queue_notify(queue);
    scheduler_block(this_cpu()->scheduler.current_thread);

    uint8_t status = *(uint8_t*) (request_paddr + HIGH_VMA + sizeof(struct virtio_blk_request));

    pmm_free(request_paddr, 1);

    return (status == VIRTIO_BLK_S_OK) ? 0 : -EIO;
}

static int virtio_blk_flush(struct virtio_blk_device* device) {
    semaphore_wait(&device->queue_semaphore);

    struct virtio_queue* queue = &device->vio_dev->queues[0];
    spinlock_acquire(&queue->lock);

    uint16_t desc0 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc1 = virtio_queue_alloc_descriptor(queue);

    uintptr_t request_paddr = pmm_alloc(1);

    struct virtio_blk_request* request = (void*) (request_paddr + HIGH_VMA);
    request->type = VIRTIO_BLK_T_FLUSH;
    request->reserved = 0;
    request->sector = 0;

    queue->descriptors[desc0].address = request_paddr;
    queue->descriptors[desc0].length = sizeof(struct virtio_blk_request);
    queue->descriptors[desc0].flags = VIRTQ_DESC_F_NEXT;
    queue->descriptors[desc0].next = desc1;

    queue->descriptors[desc1].address = request_paddr + sizeof(struct virtio_blk_request);
    queue->descriptors[desc1].length = 1;
    queue->descriptors[desc1].flags = VIRTQ_DESC_F_WRITE;
    queue->descriptors[desc1].next = 0;

    device->queue_waiters[desc0] = this_cpu()->scheduler.current_thread;
    virtio_queue_insert(queue, desc0);

    spinlock_release(&queue->lock);

    virtio_queue_notify(queue);
    scheduler_block(this_cpu()->scheduler.current_thread);

    uint8_t status = *(uint8_t*) (request_paddr + HIGH_VMA + sizeof(struct virtio_blk_request));

    pmm_free(request_paddr, 1);

    return (status == VIRTIO_BLK_S_OK) ? 0 : -EIO;
}

static ssize_t virtio_blk_cmd_handler(struct block_device* block_device, block_cmd_t cmd, uint64_t lba, size_t block_count, uintptr_t paddr) {
    struct virtio_blk_device* device = block_device->private;

    if (cmd == CMD_WRITE && device->features & VIRTIO_BLK_F_RO) {
        return -EIO;
    }

    if (cmd == CMD_FLUSH) {
        if (!(device->features & VIRTIO_BLK_F_FLUSH)) {
            return -EIO;
        }

        return virtio_blk_flush(device);
    }

    size_t remaining_blocks = block_count;
    while (remaining_blocks > 0) {
        size_t chunk_size = MIN(remaining_blocks, device->io_size_blocks);
        size_t bytes = chunk_size * block_device->block_size;

        int ret = virtio_blk_rw(device, lba, bytes, paddr, cmd == CMD_WRITE);
        if (ret < 0) {
            return ret;
        }

        remaining_blocks -= chunk_size;
        lba += chunk_size;
        paddr += bytes;
    }

    return block_count;
}

static void virtio_blk_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_blk_device* device = ctx;
    struct virtio_queue* queue = &device->vio_dev->queues[0];

    bool int_state = spinlock_acquire_irqsave(&queue->lock);

    while (queue->last_used != queue->used->index) {
        uint16_t index = queue->last_used++ % queue->size;
        uint16_t desc0 = queue->used->ring[index].id;

        scheduler_unblock(device->queue_waiters[desc0]);
        device->queue_waiters[desc0] = NULL;
        semaphore_signal(&device->queue_semaphore);

        virtio_queue_free_descriptor(queue, desc0);
    }

    spinlock_release_irqsave(&queue->lock, int_state);
}

void virtio_blk_init(struct virtio_device* vio_dev) {
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER);

    uint64_t features = VIRTIO_BLK_F_SIZE_MAX | VIRTIO_BLK_F_SEGMENT_MAX | VIRTIO_BLK_F_GEOMETRY_BIT | VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLOCK_SIZE | VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_TOPOLOGY;
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

    struct virtio_blk_config* blk_config = vio_dev->device_config;

    size_t block_count = mmio_read64(&blk_config->capacity);
    size_t block_size = (features & VIRTIO_BLK_F_BLOCK_SIZE) ? mmio_read32(&blk_config->block_size) : 512;

    size_t total_size;
    if (__builtin_mul_overflow(block_count, block_size, &total_size)) {
        kpanic(NULL, false, "VirtIO block device disk size overflow");
    }

    struct virtio_blk_device* device = kmalloc(sizeof(struct virtio_blk_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO block device\n");
    }
    device->vio_dev = vio_dev;
    device->features = features;

    size_t io_size = PAGE_SIZE_4KB;
    if (features & VIRTIO_BLK_F_SIZE_MAX) {
        size_t max_size = mmio_read32(&blk_config->size_max);
        if (max_size != 0) {
            io_size = MIN(io_size, max_size);
        }
    }

    if (features & VIRTIO_BLK_F_TOPOLOGY) {
        size_t optimal_io_size = mmio_read32(&blk_config->topology.optimal_io_size);
        if (optimal_io_size != 0) {
            device->io_size_blocks = MIN(io_size, optimal_io_size);
        }
    }

    device->io_size_blocks = DIV_CEIL(io_size, block_size);

    device->queue_waiters = kmalloc(sizeof(struct thread*) * vio_dev->queues[0].size);
    if (unlikely(device->queue_waiters == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO block device queued threads");
    }

    semaphore_init(&device->queue_semaphore, vio_dev->queues[0].size);

    isr_register_handler(vector, virtio_blk_irq_handler, device);
    mmio_write8(&vio_dev->common_config->status, mmio_read8(&vio_dev->common_config->status) | VIRTIO_STATUS_DRIVER_OK);

    klog("[virtio_blk] initialized VirtIO block device (size: %zuGB, block size: %zuB)\n", total_size / 1000000000, block_size);

    char name[10];
    snprintf(name, sizeof(name) - 1, "vioblk%zu", virtio_blk_device_minor);

    struct block_device block_device = {
        .cmd_handler = virtio_blk_cmd_handler,
        .private = device,
        .block_count = block_count,
        .block_size = block_size,
        .lba_offset = 0,
    };

    int ret = block_register(name, makedev(VIOBLK_DEV_MAJOR, virtio_blk_device_minor), &block_device, true);
    if (ret < 0) {
        kfree(device);
        return;
    }

    virtio_blk_device_minor++;
}
