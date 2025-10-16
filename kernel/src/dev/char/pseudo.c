#include <dev/char/pseudo.h>
#include <errno.h>
#include <fs/devfs.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/usercopy.h>

static ssize_t pseudo_read(int minor, void* buf, size_t count, off_t offset, int flags);
static ssize_t pseudo_write(int minor, const void* buf, size_t count, off_t offset, int flags);
static int pseudo_ioctl(int minor, int request, void* argp);

static struct device_ops pseudo_ops = {
    .read = pseudo_read,
    .write = pseudo_write,
    .ioctl = pseudo_ioctl,
};

static ssize_t pseudo_read(int minor, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor) {
        case PSEUDO_DEV_NULL_MINOR:
            ret = 0;
            break;
        case PSEUDO_DEV_ZERO_MINOR:
        case PSEUDO_DEV_FULL_MINOR:
            USER_MEMSET_MAYBE_USER(buf, 0, count);
            ret = count;
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

static ssize_t pseudo_write(int minor, const void* buf, size_t count, off_t offset, int flags) {
    (void) buf;
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor) {
        case PSEUDO_DEV_NULL_MINOR:
        case PSEUDO_DEV_ZERO_MINOR:
            ret = count;
            break;
        case PSEUDO_DEV_FULL_MINOR:
            ret = -ENOSPC;
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

static int pseudo_ioctl(int minor, int request, void* argp) {
    (void) minor;
    (void) request;
    (void) argp;
    return -ENODEV;
}

void pseudo_dev_init(void) {
    if (unlikely(devfs_register_device("null", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_NULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create null device");
    }
    if (unlikely(devfs_register_device("zero", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_ZERO_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create zero device");
    }
    if (unlikely(devfs_register_device("full", VFS_TYPE_CHARDEV, &pseudo_ops, makedev(PSEUDO_DEV_MAJOR, PSEUDO_DEV_FULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create full device");
    }
}
