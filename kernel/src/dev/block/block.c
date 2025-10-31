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
#include <utils/usercopy.h>

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

    ssize_t read = device->cmd_handler(device, CMD_READ, (uint64_t) offset / device->block_size, count / device->block_size, paddr);
    if (read < 0) {
        ret = read;
        goto end;
    } else {
        read *= device->block_size;
    }

    if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf, (void*) (paddr + HIGH_VMA), count)) < 0) {
        goto end;
    }

    ret = read;

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

    ssize_t written = device->cmd_handler(device, CMD_WRITE, (uint64_t) offset / device->block_size, count / device->block_size, paddr);
    if (written > 0) {
        written *= device->block_size;
    }

    ret = written;

end:
    pmm_free(paddr, page_count);
    return ret;
}

int block_register(const char* name, dev_t dev, block_cmd_handler_t cmd_handler, void* private, size_t block_count, size_t block_size) {
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
    device->cmd_handler = cmd_handler;
    device->private = private;
    device->block_count = block_count;
    device->block_size = block_size;

    if (unlikely(!hashmap_set(block_devices, &dev, sizeof(dev), device))) {
        spinlock_release(&block_devices_lock);
        return -ENOMEM;
    }

    int ret = devfs_register(name, VFS_TYPE_BLOCKDEV, &block_device_ops, dev);

    spinlock_release(&block_devices_lock);
    return ret;
}

void block_init(void) {
    block_devices = hashmap_create(20);
    if (unlikely(block_devices == NULL)) {
        kpanic(NULL, false, "failed to create block device map");
    }
}
