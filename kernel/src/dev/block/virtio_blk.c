#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/block/virtio_blk.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/scheduler.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

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
    uint8_t status;
} __attribute__((packed));

struct virtio_blk_io_waiter {
    struct thread* thread;
    uintptr_t paddr;
    struct virtio_blk_io_waiter* next;
};

struct virtio_blk_device {
    struct virtio_device* dev;
    uint64_t features;
    size_t sector_count;
    size_t sector_size;
    struct virtio_blk_io_waiter* io_waiter_list;
};

static bool send_command(struct virtio_blk_device* blk_dev, uint32_t type, uint64_t lba, uintptr_t paddr, size_t block_count) {
    if (type == VIRTIO_BLK_T_OUT && blk_dev->features & VIRTIO_BLK_F_RO) {
        return false;
    }

    struct virtio_queue* queue = &blk_dev->dev->queues[0];

    spinlock_acquire(&queue->lock);

    uint16_t desc0 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc1 = virtio_queue_alloc_descriptor(queue);
    uint16_t desc2 = virtio_queue_alloc_descriptor(queue);
    if (desc0 == 0xffff || desc1 == 0xffff || desc2 == 0xffff) {
        spinlock_release(&queue->lock);
        return false;
    }

    uintptr_t request_paddr = pmm_alloc(1);

    struct virtio_blk_request* request = (void*) (request_paddr + HIGH_VMA);
    request->type = type;
    request->reserved = 0;
    request->sector = lba;
    request->status = 0;

    queue->descriptors[desc0].address = request_paddr;
    queue->descriptors[desc0].length = 16;
    queue->descriptors[desc0].flags = VIRTQ_DESC_F_NEXT;
    queue->descriptors[desc0].next = desc1;

    queue->descriptors[desc1].address = paddr;
    queue->descriptors[desc1].length = block_count * blk_dev->sector_size;
    queue->descriptors[desc1].flags = VIRTQ_DESC_F_NEXT;
    if (type == VIRTIO_BLK_T_IN) {
        queue->descriptors[desc1].flags |= VIRTQ_DESC_F_WRITE;
    }
    queue->descriptors[desc1].next = desc2;

    queue->descriptors[desc2].address = request_paddr + 16;
    queue->descriptors[desc2].length = 1;
    queue->descriptors[desc2].flags = VIRTQ_DESC_F_WRITE;
    queue->descriptors[desc2].next = 0;

    struct virtio_blk_io_waiter* io_waiter = kmalloc(sizeof(struct virtio_blk_io_waiter));
    if (unlikely(io_waiter == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO I/O waiter");
    }
    io_waiter->thread = this_cpu()->running_thread;
    io_waiter->paddr = paddr;

    SLIST_PUSH_BACK(blk_dev->io_waiter_list, io_waiter);

    virtio_queue_insert(queue, desc0);
    virtio_queue_notify(queue);

    spinlock_release(&queue->lock);

    scheduler_thread_block(this_cpu()->running_thread);

    uint8_t status = *(uint8_t*) (request_paddr + HIGH_VMA + 16);
    return status == VIRTIO_BLK_S_OK ? true : false;
}

static void virtio_blk_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct virtio_blk_device* blk_dev = ctx;
    struct virtio_queue* queue = &blk_dev->dev->queues[0];

    spinlock_acquire(&queue->lock);

    for (uint16_t i = queue->last_used; i != queue->used->index; i = (i + 1) % queue->size) {
        uint16_t desc0 = queue->used->ring[i].id;

        uint16_t desc1 = queue->descriptors[desc0].next;
        uintptr_t paddr = queue->descriptors[desc1].address;

        struct virtio_blk_io_waiter* iter;
        SLIST_FOREACH(blk_dev->io_waiter_list, iter) {
            if (iter->paddr == paddr) {
                struct thread* thread = iter->thread;
                SLIST_REMOVE(blk_dev->io_waiter_list, iter);
                kfree(iter);
                scheduler_thread_unblock(thread);
                break;
            }
        }

        virtio_queue_free_descriptor(queue, desc0);
        pmm_free(queue->descriptors[desc0].address, 1);
    }

    queue->last_used = queue->used->index;

    spinlock_release(&queue->lock);
}

void virtio_blk_init(struct virtio_device* dev) {
    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_DRIVER);

    uint64_t features = VIRTIO_BLK_F_RO | VIRTIO_BLK_F_FLUSH;
    if ((features = virtio_negotiate_features(dev, features)) == (uint64_t) -1) {
        klog("[virtio_blk] failed to negotiate device features\n");
        return;
    }

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for VirtIO block device");
    }

    if (!virtio_queue_init(dev, 0, vector)) {
        klog("[virtio_blk] failed to initialize VirtIO block device request queue\n");
        return;
    }

    struct virtio_blk_device* blk_dev = kmalloc(sizeof(struct virtio_blk_device));
    if (unlikely(blk_dev == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VirtIO block device\n");
    }
    blk_dev->dev = dev;
    blk_dev->features = features;
    blk_dev->sector_count = ((struct virtio_blk_config*) dev->device_config)->capacity;
    blk_dev->sector_size = 512;

    isr_register_handler(vector, virtio_blk_irq_handler, blk_dev);

    klog("[virtio_blk] initialized VirtIO block device (size: %zuGB, block size: %zuB)\n",
         (blk_dev->sector_count * blk_dev->sector_size) / 1000000000, blk_dev->sector_size);

    mmio_write8(&dev->common_config->status, mmio_read8(&dev->common_config->status) | VIRTIO_STATUS_DRIVER_OK);
}
