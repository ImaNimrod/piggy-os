#include <dev/char/streams.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <types.h>
#include <utils/log.h>
#include <utils/random.h>
#include <utils/string.h>

static ssize_t null_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    (void) count;
    return 0;
}

static ssize_t null_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    return count;
}

static ssize_t random_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) node;
    (void) offset;
    rand_fill(buf, count);
    return count;
}

static ssize_t random_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    return count;
}

static ssize_t zero_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) node;
    (void) offset;
    memset(buf, 0, count);
    return count;
}

static ssize_t zero_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    return count;
}

void streams_init(void) {
    struct device null_dev = {
        .name = "null",
        .mode = S_IFCHR,
        .read = null_read,
        .write = null_write,
    };

    struct device random_dev = {
        .name = "random",
        .mode = S_IFCHR,
        .read = random_read,
        .write = random_write,
    };

    struct device zero_dev = {
        .name = "zero",
        .mode = S_IFCHR,
        .read = zero_read,
        .write = zero_write,
    };

    if (!devfs_add_device(&null_dev)) {
        kpanic(NULL, "failed to add null device to devfs");
    }

    if (!devfs_add_device(&random_dev)) {
        kpanic(NULL, "failed to add random device to devfs");
    }

    if (!devfs_add_device(&zero_dev)) {
        kpanic(NULL, "failed to add zero device to devfs");
    }
}
