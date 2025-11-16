#include <dev/block/block.h>
#include <errno.h>
#include <fs/devfs.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h>

#include "../../utils/printf/printf.h"

#define GPT_ATTRIBUTE_IMPORTANT (1 << 0)
#define GPT_ATTRIBUTE_DONTMOUNT (1 << 1)
#define GPT_ATTRIBUTE_LEGACY    (1 << 2)

struct gpt_header {
    char signature[8];
    uint32_t revision;
    uint32_t length;
    uint32_t crc32;
    uint32_t : 32;

    uint64_t lba;
    uint64_t altlba;
    uint64_t first;
    uint64_t last;

    uint64_t guid_low;
    uint64_t guid_high;

    uint64_t entry_lba_start;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t crc32arr;
} __attribute__((packed));

struct gpt_entry {
    uint64_t type_guid_low;
    uint64_t type_guid_high;

    uint64_t guid_low;
    uint64_t guid_high;

    uint64_t start_lba;
    uint64_t end_lba;

    uint64_t attributes;

    uint16_t name[36];
} __attribute__((packed));

struct mbr_entry {
    uint8_t status;
    uint8_t start[3];
    uint8_t type;
    uint8_t end[3];
    uint32_t start_sector;
    uint32_t sector_count;
} __attribute__((packed));

static dev_t partition_device_minor;

static ssize_t block_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);
static ssize_t block_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);

static struct device_ops block_device_ops = {
    .read = block_read,
    .write = block_write,
};

static hashmap_t* block_devices;
static spinlock_t block_devices_lock;

// TODO: support O_DIRECT flag and accesses that are not of at offset or of size divisble by block_size

static ssize_t block_read(dev_t dev, void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    struct block_device* device;

    spinlock_acquire(&block_devices_lock);
    if (!hashmap_get(block_devices, &dev, sizeof(dev), (void**) &device)) {
        spinlock_release(&block_devices_lock);
        return -ENODEV;
    }
    spinlock_release(&block_devices_lock);

    if ((count % device->block_size) != 0 || (offset % device->block_size) != 0) {
        return -EINVAL;
    }

    ssize_t ret;

    size_t page_count = DIV_CEIL(count, PAGE_SIZE_4KB);
    uintptr_t paddr = pmm_alloc(page_count);

    ssize_t read = device->cmd_handler(device, CMD_READ, ((uint64_t) offset / device->block_size) + device->lba_offset, count / device->block_size, paddr);
    if (read < 0) {
        ret = read;
        goto end;
    }

    if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf, (void*) (paddr + HIGH_VMA), count)) < 0) {
        goto end;
    }

    ret = read * device->block_size;

end:
    pmm_free(paddr, page_count);
    return ret;
}

static ssize_t block_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    struct block_device* device;

    spinlock_acquire(&block_devices_lock);
    if (!hashmap_get(block_devices, &dev, sizeof(dev), (void**) &device)) {
        spinlock_release(&block_devices_lock);
        return -ENODEV;
    }
    spinlock_release(&block_devices_lock);

    if ((count % device->block_size) != 0 || (offset % device->block_size) != 0) {
        return -EINVAL;
    }

    size_t page_count = DIV_CEIL(count, PAGE_SIZE_4KB);
    uintptr_t paddr = pmm_alloc(page_count);

    ssize_t ret = USER_MEMCPY_MAYBE_FROM_USER((void*) (paddr + HIGH_VMA), buf, count);
    if (ret < 0) {
        goto end;
    }

    ssize_t written = device->cmd_handler(device, CMD_WRITE, ((uint64_t) offset / device->block_size) + device->lba_offset, count / device->block_size, paddr);
    if (written > 0) {
        written *= device->block_size;
    }

    ret = written;

end:
    pmm_free(paddr, page_count);
    return ret;
}

static void detect_partitions(struct block_device* device, const char* device_name) {
    size_t name_len = strlen(device_name) + 6;
    char name[name_len];

    size_t partition_number = 0;

    size_t page_count = DIV_CEIL(2 * device->block_size, PAGE_SIZE_4KB);
    uintptr_t paddr = pmm_alloc(page_count);

    if (device->cmd_handler(device, CMD_READ, 0, 2, paddr) < 0) {
        goto end;
    }

    uint8_t* buf = (uint8_t*) (paddr + HIGH_VMA);

    struct gpt_header* gpt_header = (void*) ((uintptr_t) buf + 512);

    if (!strncmp(gpt_header->signature, "EFI PART", sizeof(gpt_header->signature))) {
        if (gpt_header->length < sizeof(struct gpt_header)) {
            goto end;
        }
        if (gpt_header->lba != 1) {
            goto end;
        }
        if (gpt_header->first > gpt_header->last) {
            goto end;
        }

        size_t table_size = gpt_header->entry_count * gpt_header->entry_size;
        size_t table_page_count = DIV_CEIL(table_size, PAGE_SIZE_4KB);
        uintptr_t table_paddr = pmm_alloc(table_page_count);

        if (device->cmd_handler(device, CMD_READ, gpt_header->entry_lba_start, DIV_CEIL(table_size, device->block_size), table_paddr) < 0) {
            pmm_free(table_paddr, table_page_count);
            goto end;
        }

        void* table_buffer = (void*) (table_paddr + HIGH_VMA);

        for (size_t i = 0; i < gpt_header->entry_count; i++) {
            struct gpt_entry* entry = (struct gpt_entry*) ((uintptr_t) table_buffer + (gpt_header->entry_size * i));
            if (entry->guid_low == 0 && entry->guid_high == 0) {
                continue;
            }
            if (entry->attributes & (GPT_ATTRIBUTE_DONTMOUNT | GPT_ATTRIBUTE_LEGACY)) {
                continue;
            }

            snprintf(name, name_len - 1, "%sp%u", device_name, partition_number);

            struct block_device block_device = {
                .cmd_handler = device->cmd_handler,
                .private = NULL,
                .block_count = entry->end_lba - entry->start_lba,
                .block_size = device->block_size,
                .lba_offset = entry->start_lba,
            };

            block_register(name, makedev(PARTITION_DEV_MAJOR, partition_device_minor), &block_device, false);

            klog("[block] found GPT partition %s\n", name);

            partition_number++;
            partition_device_minor++;
        }

        pmm_free(table_paddr, table_page_count);
    } else if (*((uint16_t*) (buf + 510)) == 0xaa55) {
        struct mbr_entry* entries = (struct mbr_entry*) (buf + 0x1be);

        for (size_t i = 0; i < 4; i++) {
            struct mbr_entry* entry = &entries[i];
            if (entry->type == 0) {
                continue;
            }

            snprintf(name, name_len - 1, "%sp%u", device_name, partition_number);

            struct block_device block_device = {
                .cmd_handler = device->cmd_handler,
                .private = NULL,
                .block_count = entry->sector_count,
                .block_size = device->block_size,
                .lba_offset = entry->start_sector,
            };

            block_register(name, makedev(PARTITION_DEV_MAJOR, partition_device_minor), &block_device, false);

            klog("[block] found MBR partition %s\n", name);

            partition_number++;
            partition_device_minor++;
        }
    }

end:
    pmm_free(paddr, page_count);
}

int block_register(const char* name, dev_t dev, struct block_device* block_device, bool check_partitions) {
    struct block_device* temp;

    spinlock_acquire(&block_devices_lock);
    if (hashmap_get(block_devices, &dev, sizeof(dev), (void**) &temp)) {
        spinlock_release(&block_devices_lock);
        return -EEXIST;
    }

    struct block_device* device = kmalloc(sizeof(struct block_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for struct block_device");
    }
    memcpy(device, block_device, sizeof(struct block_device));

    bool ret = hashmap_set(block_devices, &dev, sizeof(dev), device);

    spinlock_release(&block_devices_lock);
    if (unlikely(!ret)) {
        return -ENOMEM;
    }

    int ret2 = devfs_register(name, VFS_TYPE_BLOCKDEV, &block_device_ops, dev);

    if (ret2 <= 0 && check_partitions) {
        detect_partitions(device, name);
    }

    return ret2;
}

void block_init(void) {
    block_devices = hashmap_create(20);
    if (unlikely(block_devices == NULL)) {
        kpanic(NULL, false, "failed to create block device map");
    }
}
