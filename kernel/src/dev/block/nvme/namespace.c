#include <cpu/smp.h>
#include <dev/block/block.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <printf.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"

struct namespace_identify {
    uint64_t lbasize;
    uint64_t lbacapacity;
    uint64_t lbautilized;
    uint8_t features;
    uint8_t lbaformatcount;
    uint8_t lbaformattedsize;
    uint8_t metadatacap;
    uint8_t endtoendprot;
    uint8_t endtoendprotsettings;
    uint8_t sharingcap;
    uint8_t rescap;
    uint8_t fpi;
    uint8_t deallocate;
    uint16_t atomicwrite;
    uint16_t atomicwritepowerfail;
    uint16_t atomiccomparewrite;
    uint16_t atomicboundarysize;
    uint16_t atomicboundaryoffset;
    uint16_t atomicboundarypowerfail;
    uint16_t optimalioboundary;
    uint64_t nvmcapacity[2];
    uint16_t preferredwritegranularity;
    uint16_t preferredwritealignment;
    uint16_t preferreddeallocategranularity;
    uint16_t preferreddeallocatealignment;
    uint16_t optimalwritesize;
    uint8_t reserved1[18];
    uint32_t anagrpid;
    uint8_t reserved2[3];
    uint8_t namespaceattr;
    uint16_t nvmsetid;
    uint16_t endgid;
    uint64_t namespaceguid[2];
    uint64_t eui64;
    struct {
        uint16_t metadatasize;
        uint8_t lbadatasize;
        uint8_t relativeperformance;
    } lbaformat[16];
} __attribute__((packed));

static dev_t nvme_device_minor;

static ssize_t nvme_namespace_cmd_handler(struct block_device* block_device, block_cmd_t cmd, uint64_t lba, size_t block_count, uintptr_t paddr) {
    struct nvme_namespace* namespace = block_device->private;
    struct nvme_controller* controller = namespace->controller;

    uint16_t io_queue_index = 0;
    if (controller->io_queue_count > 1) {
        uint16_t cpu_number = this_cpu()->cpu_number;
        uint16_t usable = controller->io_queue_count - 1;
        if (cpu_number < usable) {
            io_queue_index = cpu_number + 1;
        } else {
            io_queue_index = (cpu_number % usable) + 1;
        }
    }

    struct queue_pair* queue_pair = &controller->io_queues[io_queue_index];

    struct entry_pair entry_pair = {0};
    entry_pair.submission.nsid = namespace->id;

    if (cmd == CMD_FLUSH) {
        (void) lba;
        (void) block_count;
        (void) paddr;
        entry_pair.submission.opcode = NVME_OP_FLUSH;
    } else {
        entry_pair.submission.opcode = cmd == CMD_READ ? NVME_OP_READ : NVME_OP_WRITE;
        entry_pair.submission.prp[0] = paddr;
        entry_pair.submission.command[0] = lba & 0xffffffff;
        entry_pair.submission.command[1] = (lba >> 32) & 0xffffffff;
        entry_pair.submission.command[2] = (block_count - 1) & 0xffff;
    }

    if (!run_command(queue_pair, &entry_pair)) {
        return -EIO;
    }

    return block_count;
}

void namespace_init(struct nvme_controller* controller, int id) {
    uintptr_t identity_paddr = pmm_alloc(1);

    if (!identify(controller, id, IDENTIFY_NAMESPACE, identity_paddr)) {
        klog("[nvme] failed to identify NVMe controller namespace %u\n", id);
        pmm_free(identity_paddr, 1);
        return;
    }

    struct namespace_identify* namespace_identify = (void*) (identity_paddr + HIGH_VMA);

    size_t lba_count = namespace_identify->lbacapacity;
    size_t lba_size = (1 << namespace_identify->lbaformat[namespace_identify->lbaformattedsize & 0x0f].lbadatasize);

    size_t total_size;
    if (__builtin_mul_overflow(lba_count, lba_size, &total_size)) {
        kpanic(NULL, false, "NVMe device size overflow");
    }

    struct nvme_namespace* namespace = kmalloc(sizeof(struct nvme_namespace));
    if (unlikely(namespace == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for NVMe namespace");
    }
    namespace->id = id;
    namespace->controller = controller;

    klog("[nvme] initialized namespace %u (size: %zuGB, block size: %zuB)\n",
            id, total_size / 1000000000, lba_size);

    char name[12];
    snprintf(name, sizeof(name) - 1, "nvme%un%u", controller->id, id);

    struct block_device block_device = {
        .cmd_handler = nvme_namespace_cmd_handler,
        .private = namespace,
        .block_count = lba_count,
        .block_size = lba_size,
        .lba_offset = 0,
    };

    if (unlikely(block_register(name, makedev(NVME_DEV_MAJOR, nvme_device_minor++), &block_device, true) < 0)) {
        kfree(namespace);
    }

    pmm_free(identity_paddr, 1);
}
