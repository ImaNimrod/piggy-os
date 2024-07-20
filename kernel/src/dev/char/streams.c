#include <dev/char/streams.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <types.h>
#include <utils/log.h>
#include <utils/random.h>
#include <utils/string.h>

static ssize_t stream_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) offset;

    spinlock_acquire(&node->lock);

    ssize_t read = -1;

    switch (minor(node->stat.st_rdev)) {
        case STREAM_NULL_MIN:
            read = 0;
            break;
        case STREAM_ZERO_MIN:
            memset(buf, 0, count);
            read = count;
            break;
        case STREAM_RANDOM_MIN:
            rng_rand_fill((struct rng_state*) node->private, buf, count);
            read = count;
            break;
    }

    spinlock_release(&node->lock);
    return read;
}

static ssize_t stream_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) buf;
    (void) offset;

    spinlock_acquire(&node->lock);

    ssize_t written = -1;

    switch (minor(node->stat.st_rdev)) {
        case STREAM_NULL_MIN:
        case STREAM_ZERO_MIN:
        case STREAM_RANDOM_MIN:
            written = count;
            break;
    }

    spinlock_release(&node->lock);
    return written;
}

void streams_init(void) {
    struct stat stream_stat = {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
    };

    struct vfs_node* null_dev = devfs_create_device("null");
    if (null_dev == NULL) {
        kpanic(NULL, "failed to create null device for devfs");
    }

    memcpy(&null_dev->stat, &stream_stat, sizeof(struct stat));
    null_dev->stat.st_rdev = makedev(STREAM_MAJ, STREAM_NULL_MIN);

    null_dev->read = stream_read;
    null_dev->write = stream_write;

    struct vfs_node* zero_dev = devfs_create_device("zero");
    if (zero_dev == NULL) {
        kpanic(NULL, "failed to create zero device for devfs");
    }

    memcpy(&zero_dev->stat, &stream_stat, sizeof(struct stat));
    zero_dev ->stat.st_rdev = makedev(STREAM_MAJ, STREAM_ZERO_MIN);

    zero_dev->read = stream_read;
    zero_dev->write = stream_write;

    struct rng_state* rng = rng_create();
    if (rng == NULL) {
        kpanic(NULL, "failed to initialize PRNG for random device");
    }

    struct vfs_node* random_dev = devfs_create_device("random");
    if (random_dev == NULL) {
        kpanic(NULL, "failed to create random device for devfs");
    }

    memcpy(&random_dev->stat, &stream_stat, sizeof(struct stat));
    random_dev->stat.st_rdev = makedev(STREAM_MAJ, STREAM_RANDOM_MIN);

    random_dev->private = rng;

    random_dev->read = stream_read;
    random_dev->write = stream_write;
}
