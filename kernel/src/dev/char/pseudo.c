#include <dev/char/pseudo.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <sys/time.h>
#include <types.h>
#include <utils/panic.h>
#include <utils/random.h>
#include <utils/string.h>

static ssize_t pseudo_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) offset;
    (void) flags;

    ssize_t read = -1;

    switch (minor(node->stat.st_rdev)) {
        case PSEUDO_NULL_MIN:
            read = 0;
            break;
        case PSEUDO_ZERO_MIN:
        case PSEUDO_FULL_MIN:
            memset(buf, 0, count);
            read = count;
            break;
        case PSEUDO_RANDOM_MIN:
            rng_rand_fill((struct rng_state*) node->private, buf, count);
            read = count;
            break;
    }

    return read;
}

static ssize_t pseudo_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) buf;
    (void) offset;
    (void) flags;

    ssize_t written = -1;

    switch (minor(node->stat.st_rdev)) {
        case PSEUDO_NULL_MIN:
        case PSEUDO_ZERO_MIN:
        case PSEUDO_RANDOM_MIN:
            written = count;
            break;
        case PSEUDO_FULL_MIN:
            written = count == 0 ? 0 : -ENOSPC;
            break;
    }

    return written;
}

__attribute__((section(".unmap_after_init")))
void pseudo_init(void) {
    struct stat pseudo_stat = {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
        .st_atim = time_realtime,
        .st_mtim = time_realtime,
        .st_ctim = time_realtime
    };

    struct vfs_node* null_node = devfs_create_device("null");
    if (null_node == NULL) {
        kpanic(NULL, false, "failed to create null device node in devfs");
    }

    memcpy(&null_node->stat, &pseudo_stat, sizeof(struct stat));
    null_node->stat.st_rdev = makedev(PSEUDO_MAJ, PSEUDO_NULL_MIN);

    null_node->read = pseudo_read;
    null_node->write = pseudo_write;

    struct vfs_node* zero_node = devfs_create_device("zero");
    if (zero_node == NULL) {
        kpanic(NULL, false, "failed to create zero device node in devfs");
    }

    memcpy(&zero_node->stat, &pseudo_stat, sizeof(struct stat));
    zero_node ->stat.st_rdev = makedev(PSEUDO_MAJ, PSEUDO_ZERO_MIN);

    zero_node->read = pseudo_read;
    zero_node->write = pseudo_write;

    struct rng_state* rng = rng_create();
    if (rng == NULL) {
        kpanic(NULL, false, "failed to initialize PRNG for random device");
    }

    struct vfs_node* random_node = devfs_create_device("random");
    if (random_node == NULL) {
        kpanic(NULL, false, "failed to create random device node in devfs");
    }

    memcpy(&random_node->stat, &pseudo_stat, sizeof(struct stat));
    random_node->stat.st_rdev = makedev(PSEUDO_MAJ, PSEUDO_RANDOM_MIN);

    random_node->private = rng;

    random_node->read = pseudo_read;
    random_node->write = pseudo_write;

    struct vfs_node* full_node = devfs_create_device("full");
    if (full_node == NULL) {
        kpanic(NULL, false, "failed to create full device node in devfs");
    }

    memcpy(&full_node->stat, &pseudo_stat, sizeof(struct stat));
    full_node->stat.st_rdev = makedev(PSEUDO_MAJ, PSEUDO_FULL_MIN);

    full_node->read = pseudo_read;
    full_node->write = pseudo_write;
}
