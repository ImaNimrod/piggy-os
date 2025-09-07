#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/macros.h>
#include <utils/panic.h>

struct devfs_node {
    struct vfs_node;
};

static struct slab_cache* devfs_node_cache = NULL;
static struct devfs_node* devfs_root_node = NULL;

static int devfs_mount(struct vfs_node* backing, struct vfs_node* filesystem, struct vfs_filesystem** result);
static int devfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops devfs_ops = {
    .mount = devfs_mount,
    .root = devfs_root,
};

static int devfs_lock(struct vfs_node* node);
static int devfs_unlock(struct vfs_node* node);
static void devfs_inactive(struct vfs_node* node);

static struct vfs_node_ops devfs_node_ops = {
    .lock = devfs_lock,
    .unlock = devfs_unlock,
    .inactive = devfs_inactive,
};

static int devfs_mount(struct vfs_node* backing, struct vfs_node* filesystem, struct vfs_filesystem** result) {
    (void) backing;
    (void) filesystem;

    struct vfs_filesystem* vfs = kmalloc(sizeof(struct vfs_filesystem));
    if (unlikely(vfs == NULL)) {
        return -1;
    }

    *result = vfs;
    vfs->ops = &devfs_ops;

    return 0;
}

static int devfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    devfs_root_node->filesystem = filesystem;
    *result = (struct vfs_node*) devfs_root_node;
    return 0;
}

static int devfs_lock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int devfs_unlock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static void devfs_inactive(struct vfs_node* node) {
    slab_cache_free(devfs_node_cache, node);
}

void devfs_init(void) {
    devfs_node_cache = slab_cache_create("struct devfs_node cache", sizeof(struct devfs_node));
    if (unlikely(devfs_node_cache == NULL)) {
        kpanic(NULL, false, "failed to create object cache for devfs nodes");
    }

    devfs_root_node = slab_cache_alloc(devfs_node_cache);
    if (unlikely(devfs_root_node == NULL)) {
        kpanic(NULL, false, "failed to create devfs root node");
    }

    devfs_root_node->type = VFS_TYPE_DIRECTORY;
    devfs_root_node->flags = VFS_FLAG_ROOT;
    devfs_root_node->ops = &devfs_node_ops;
    devfs_root_node->refcount = 1;

    if (unlikely(!vfs_register_fs("devfs", &devfs_ops))) {
        kpanic(NULL, false, "failed to register devfs with vfs");
    }
}
