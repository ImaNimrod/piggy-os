#include <errno.h>
#include <fs/devfs.h>
#include <fs/streams.h>
#include <types.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

static ssize_t stream_read(int minor, void* buf, size_t count, off_t offset, int flags);
static ssize_t stream_write(int minor, const void* buf, size_t count, off_t offset, int flags);
static int stream_ioctl(int minor, int request, void* argp);

struct device_ops stream_ops = {
    .read = stream_read,
    .write = stream_write,
    .ioctl = stream_ioctl,
};

static ssize_t stream_read(int minor, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor) {
        case STREAM_DEV_NULL_MINOR:
            ret = 0;
            break;
        case STREAM_DEV_ZERO_MINOR:
        case STREAM_DEV_FULL_MINOR:
            memset(buf, 0, count);
            ret = count;
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

static ssize_t stream_write(int minor, const void* buf, size_t count, off_t offset, int flags) {
    (void) buf;
    (void) offset;
    (void) flags;

    ssize_t ret;

    switch (minor) {
        case STREAM_DEV_NULL_MINOR:
        case STREAM_DEV_ZERO_MINOR:
            ret = count;
            break;
        case STREAM_DEV_FULL_MINOR:
            ret = -ENOSPC;
            break;
        default:
            ret = -ENODEV;
            break;
    }

    return ret;
}

static int stream_ioctl(int minor, int request, void* argp) {
    (void) minor;
    (void) request;
    (void) argp;
    return -ENODEV;
}

void streams_init(void) {
    if (unlikely(devfs_register_device("null", VFS_TYPE_CHARDEV, &stream_ops, makedev(STREAM_DEV_MAJOR, STREAM_DEV_NULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create null device");
    }
    if (unlikely(devfs_register_device("zero", VFS_TYPE_CHARDEV, &stream_ops, makedev(STREAM_DEV_MAJOR, STREAM_DEV_ZERO_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create zero device");
    }
    if (unlikely(devfs_register_device("full", VFS_TYPE_CHARDEV, &stream_ops, makedev(STREAM_DEV_MAJOR, STREAM_DEV_FULL_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create full device");
    }
}
