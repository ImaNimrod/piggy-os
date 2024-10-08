#include <dev/block/partition.h>
#include <errno.h>
#include <fs/devfs.h>
#include <mem/slab.h>
#include <stddef.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

#define GPT_ATTRIBUTES_IMPORTANT    (1 << 0)
#define GPT_ATTRIBUTES_DONTMOUNT    (1 << 1)
#define GPT_ATTRIBUTES_LEGACY       (1 << 2)

struct gpt_header {
    char signature[8];
    uint32_t revision;
    uint32_t length;
    uint32_t crc32;
    uint32_t: 32;
    uint64_t lba;
    uint64_t alt_lba;
    uint64_t first;
    uint64_t last;
    uint64_t guid_low;
    uint64_t guid_high;
    uint64_t partition_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t crc32arr;
};

struct gpt_entry {
    uint64_t type_guid_low;
    uint64_t type_guid_high;
    uint64_t unique_guid_low;
    uint64_t unique_guid_high;
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t name[36];
};

struct mbr_entry {
    uint8_t status;
    uint8_t start[3];
    uint8_t type;
    uint8_t end[3];
    uint32_t start_lba;
    uint32_t sector_count;
};

struct partition_metadata {
    struct vfs_node* device;
    uint64_t start_lba;
    uint64_t sector_count;
};

static size_t partition_count = 0;

static ssize_t partition_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    struct partition_metadata* metadata = (struct partition_metadata*) node->private;
    if (offset >= (ssize_t) metadata->sector_count * node->stat.st_blksize) {
        return 0;
    }

    return metadata->device->read(metadata->device, buf, offset + (metadata->start_lba * node->stat.st_blksize), count, flags);
}

static ssize_t partition_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    struct partition_metadata* metadata = (struct partition_metadata*) node->private;
    if (offset >= (ssize_t) metadata->sector_count * node->stat.st_blksize) {
        return 0;
    }

    return metadata->device->write(metadata->device, buf, offset + (metadata->start_lba * node->stat.st_blksize), count, flags);
}

static int partition_sync(struct vfs_node* node) {
    struct partition_metadata* metadata = (struct partition_metadata*) node->private;
    return metadata->device->sync(metadata->device);
}

static void enum_gpt(struct vfs_node* device_node, struct gpt_header* gpt_header) {
    ssize_t aligned_size = ALIGN_UP(gpt_header->entry_count * gpt_header->entry_size, device_node->stat.st_blksize);
    off_t offset = gpt_header->partition_lba * device_node->stat.st_blksize;

    uint8_t* entries = kmalloc(aligned_size);
    if (unlikely(device_node->read(device_node, entries, offset, aligned_size, 0) != aligned_size)) {
        klog("[partition] failed to read from %s\n", device_node->name);
        return;
    }

    char partition_name[12];

    struct gpt_entry* entry = (struct gpt_entry*) entries;
    for (size_t i = 0; i < gpt_header->entry_count; i++) {
        if (unlikely(entry->unique_guid_low == 0 && entry->unique_guid_high == 0)) {
            continue;
        }

        if (entry->attributes & (GPT_ATTRIBUTES_DONTMOUNT | GPT_ATTRIBUTES_LEGACY)) {
            continue;
        }

        ssize_t sector_count = entry->end_lba - entry->start_lba;

        klog("[partition] found GPT partition on %s: start LBA: %u (+%u)\n",
                device_node->name, entry->start_lba, sector_count);

        struct partition_metadata* metadata = kmalloc(sizeof(struct partition_metadata));
        if (unlikely(metadata == NULL)) {
            kpanic(NULL, false, "failed to allocate partition metadata struct");
        }

        metadata->device = device_node;
        metadata->start_lba = entry->start_lba;
        metadata->sector_count = sector_count;

        snprintf(partition_name, sizeof(partition_name), "%sp%u", device_node->name, i);

        struct vfs_node* partition_node = devfs_create_device(partition_name);
        if (unlikely(partition_node == NULL)) {
            kpanic(NULL, false, "failed to create device node for partition device %s in devfs", partition_name);
        }

        partition_node->stat = (struct stat) {
            .st_dev = makedev(0, 1),
            .st_mode = S_IFBLK,
            .st_rdev = makedev(PART_MAJ, partition_count++),
            .st_size = sector_count * device_node->stat.st_blksize,
            .st_blksize = device_node->stat.st_blksize,
            .st_blocks = sector_count,
            .st_atim = time_realtime,
            .st_mtim = time_realtime,
            .st_ctim = time_realtime,
        };

        partition_node->private = metadata;

        partition_node->read = partition_read;
        partition_node->write = partition_write;
        partition_node->sync = partition_sync;

        entry += 1;
    }

    kfree(entries);
}

static void enum_mbr(struct vfs_node* device_node) {
    uint8_t* mbr = kmalloc(device_node->stat.st_blksize);
    if (device_node->read(device_node, mbr, 0, device_node->stat.st_blksize, 0) != device_node->stat.st_blksize) {
        klog("[partition] failed to read from %s\n", device_node->name);
        return;
    }

    uint16_t hint = *(uint16_t*) (mbr + 444);
    if (hint != 0 && hint != 0x5a5a) {
        return;
    }

    char partition_name[12];

    struct mbr_entry* entry = (struct mbr_entry*) (mbr + 446);
    for (size_t i = 0; i < 4; i++) {
        if (unlikely(entry->type == 0)) {
            continue;
        }

        klog("[partition] found MBR partition on %s: start LBA: %u (+%u), type: 0x%02x\n",
                device_node->name, entry->start_lba, entry->sector_count, entry->type);

        struct partition_metadata* metadata = kmalloc(sizeof(struct partition_metadata));
        if (unlikely(metadata == NULL)) {
            kpanic(NULL, false, "failed to allocate partition metadata struct");
        }

        metadata->device = device_node;
        metadata->start_lba = entry->start_lba;
        metadata->sector_count = entry->sector_count;

        snprintf(partition_name, sizeof(partition_name), "%sp%u", device_node->name, i);

        struct vfs_node* partition_node = devfs_create_device(partition_name);
        if (unlikely(partition_node == NULL)) {
            kpanic(NULL, false, "failed to create device node for partition device %s in devfs", partition_name);
        }

        partition_node->stat = (struct stat) {
            .st_dev = makedev(0, 1),
            .st_mode = S_IFBLK,
            .st_rdev = makedev(PART_MAJ, partition_count++),
            .st_size = entry->sector_count * device_node->stat.st_blksize,
            .st_blksize = device_node->stat.st_blksize,
            .st_blocks = entry->sector_count,
            .st_atim = time_realtime,
            .st_mtim = time_realtime,
            .st_ctim = time_realtime,
        };

        partition_node->private = metadata;

        partition_node->read = partition_read;
        partition_node->write = partition_write;
        partition_node->sync = partition_sync;

        entry += 1;
    }

    kfree(mbr);
}

void partition_scan(struct vfs_node* device_node) {
    uint8_t* buf = kmalloc(device_node->stat.st_blksize);
    if (unlikely(device_node->read(device_node, buf, 512, device_node->stat.st_blksize, 0) != device_node->stat.st_blksize)) {
        klog("[partition] failed to read from %s\n", device_node->name);
        return;
    }

    struct gpt_header* gpt_header = (struct gpt_header*) buf;

    if (strncmp(gpt_header->signature, "EFI PART", 8) || gpt_header->length < 92
            || gpt_header->lba != 1 || gpt_header->first > gpt_header->last) {
        enum_mbr(device_node);
    } else {
        enum_gpt(device_node, gpt_header);
    }

    kfree(buf);
}
