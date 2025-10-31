#include <dev/char/pseudo.h>
#include <errno.h>
#include <fs/devfs.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/random.h>
#include <utils/usercopy.h>

static ssize_t pseudo_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);
static ssize_t pseudo_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);

static struct device_ops pseudo_ops = {
    .read = pseudo_read,
    .write = pseudo_write,
};

static ssize_t fill_random(uint8_t* buf, size_t count) {
    int ret;

    size_t full_chunks = count / sizeof(uint64_t);
    size_t leftover = count % sizeof(uint64_t);
    off_t offset = 0;

    for (size_t i = 0; i < full_chunks; i++) {
        uint64_t rand = rand64();
        if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf + offset, &rand, sizeof(uint64_t))) < 0) {
            return ret;
        }
        offset += sizeof(uint64_t);
    }

    if (leftover > 0) {
        uint64_t rand = rand64();
        if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf + offset, &rand, leftover)) < 0) {
            return ret;
        }
    }

    return offset;
}

static ssize_t pseudo_read(dev_t dev, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor(dev)) {
        case PSEUDO_DEV_NULL_MINOR:
            ret = 0;
            break;
        case PSEUDO_DEV_ZERO_MINOR:
        case PSEUDO_DEV_FULL_MINOR:
            if ((ret = USER_MEMSET_MAYBE_USER(buf, 0, count)) < 0) {
                return ret;
            }
            ret = count;
            break;
        case PSEUDO_DEV_RANDOM_MINOR:
            ret = fill_random((uint8_t*) buf, count);
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

static ssize_t pseudo_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) buf;
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor(dev)) {
        case PSEUDO_DEV_NULL_MINOR:
        case PSEUDO_DEV_ZERO_MINOR:
            ret = count;
            break;
        case PSEUDO_DEV_FULL_MINOR:
            ret = -ENOSPC;
            break;
        case PSEUDO_DEV_RANDOM_MINOR:
            ret = -ENOTSUP;
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

void pseudo_dev_init(void) {
    if (unlikely(devfs_register("null", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_NULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create null device");
    }
    if (unlikely(devfs_register("zero", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_ZERO_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create zero device");
    }
    if (unlikely(devfs_register("full", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_FULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create full device");
    }
    if (unlikely(devfs_register("random", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_RANDOM_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create random device");
    }
}
