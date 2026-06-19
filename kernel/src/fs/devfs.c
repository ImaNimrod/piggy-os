#include <errno.h>
#include <fs/devfs.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <sys/timer.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>
#include <utils/usercopy.h>

struct devfs_node {
    struct vfs_node;
    struct stat stat;
    struct device_ops* devops;
};

static struct slab_cache* devfs_node_cache;
static struct devfs_node* devfs_root_node;
static hashmap_t* devices;
static ino_t inode_counter = 1;

static int devfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result);
static int devfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops devfs_ops = {
    .mount = devfs_mount,
    .root = devfs_root,
};

static int devfs_parent(struct vfs_node* node, struct vfs_node** result);
static int devfs_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result);
static int devfs_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result);
static int devfs_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name);
static int devfs_link(struct vfs_node* dir, const char* name, struct vfs_node* node);
static int devfs_symlink(struct vfs_node* dir, const char* name, const char* target);
static ssize_t devfs_readlink(struct vfs_node* node, char* buf, size_t length);
static int devfs_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name);
static ssize_t devfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t devfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int devfs_ioctl(struct vfs_node* node, int request, void* argp);
static int devfs_truncate(struct vfs_node* node, off_t length);
static short devfs_poll(struct vfs_node* node, short events, struct poll_table* pt);
static int devfs_sync(struct vfs_node* node);
static int devfs_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags);
static int devfs_munmap(struct vfs_node* node, void* addr, off_t offset);
static ssize_t devfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset);
static int devfs_getstat(struct vfs_node* node, struct stat* stat);
static int devfs_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int devfs_lock(struct vfs_node* node);
static int devfs_unlock(struct vfs_node* node);
static void devfs_inactive(struct vfs_node* node);

static struct vfs_node_ops devfs_node_ops = {
    .parent = devfs_parent,
    .create = devfs_create,
    .lookup = devfs_lookup,
    .rename = devfs_rename,
    .link = devfs_link,
    .symlink = devfs_symlink,
    .readlink = devfs_readlink,
    .unlink = devfs_unlink,
    .read = devfs_read,
    .write = devfs_write,
    .ioctl = devfs_ioctl,
    .truncate = devfs_truncate,
    .poll = devfs_poll,
    .sync = devfs_sync,
    .mmap = devfs_mmap,
    .munmap = devfs_munmap,
    .getdents = devfs_getdents,
    .getstat = devfs_getstat,
    .setstat = devfs_setstat,
    .lock = devfs_lock,
    .unlock = devfs_unlock,
    .inactive = devfs_inactive,
};

static int devfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result) {
    (void) backing;
    (void) target;

    struct vfs_filesystem* devfs = kmallocz(sizeof(struct vfs_filesystem));
    if (unlikely(!devfs)) {
        return -ENOMEM;
    }
    devfs->root = (struct vfs_node*) devfs_root_node;
    devfs->ops = &devfs_ops;

    devfs_root_node->filesystem = devfs;

    *result = devfs;
    return 0;
}

static int devfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    *result = filesystem->root;
    return 0;
}

static int devfs_parent(struct vfs_node* node, struct vfs_node** result) {
    (void) node;

    VFS_NODE_REF((struct vfs_node*) devfs_root_node);
    *result = (struct vfs_node*) devfs_root_node;
    return 0;
}

static int devfs_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) type;
    (void) result;
    return -ENOTSUP;
}

static int devfs_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result) {
    if (parent != (struct vfs_node*) devfs_root_node) {
        return -ENODEV;
    }

    struct vfs_node* child;
    if (!hashmap_get(devices, name, strlen(name), (void**) &child)) {
        return -ENOENT;
    }

    VFS_NODE_REF(child);

    if (child != parent) {
        child->ops->lock(child);
    }

    *result = (struct vfs_node*) child;
    return 0;
}

static int devfs_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name) {
    (void) src_dir;
    (void) src;
    (void) old_name;
    (void) target_dir;
    (void) new_name;
    return -ENOTSUP;
}

static int devfs_link(struct vfs_node* dir, const char* name, struct vfs_node* node) {
    (void) dir;
    (void) name;
    (void) node;
    return -ENOTSUP;
}

static int devfs_symlink(struct vfs_node* dir, const char* name, const char* target) {
    (void) dir;
    (void) name;
    (void) target;
    return -ENOTSUP;
}

static ssize_t devfs_readlink(struct vfs_node* node, char* buf, size_t length) {
    (void) node;
    (void) buf;
    (void) length;
    return -ENOTSUP;
}

static int devfs_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name) {
    (void) parent;
    (void) child;
    (void) name;
    return -ENOTSUP;
}

static ssize_t devfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->read) {
        return -ENODEV;
    }

    ssize_t ret = dnode->devops->read(dnode->stat.st_rdev, buf, count, offset, flags);
    if (ret >= 0) {
        dnode->stat.st_atim = time_realtime;
    }
    return ret;
}

static ssize_t devfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->write) {
        return -ENODEV;
    }

    ssize_t ret = dnode->devops->write(dnode->stat.st_rdev, buf, count, offset, flags);
    if (ret >= 0) {
        dnode->stat.st_mtim = dnode->stat.st_ctim = time_realtime;
    }
    return ret;
}

static int devfs_ioctl(struct vfs_node* node, int request, void* argp) {
    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->ioctl) {
        return -ENOTTY;
    }

    return dnode->devops->ioctl(dnode->stat.st_rdev, request, argp);
}

static int devfs_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EPERM;
}

static short devfs_poll(struct vfs_node* node, short events, struct poll_table* pt) {
    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->poll) {
        return -ENODEV;
    }

    return dnode->devops->poll(dnode->stat.st_rdev, events, pt);
}

static int devfs_sync(struct vfs_node* node) {
    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->sync) {
        return -ENODEV;
    }

    return dnode->devops->sync(dnode->stat.st_rdev);
}

static int devfs_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags) {
    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->mmap) {
        return -ENODEV;
    }

    return dnode->devops->mmap(dnode->stat.st_rdev, addr, offset, flags, pte_flags);
}

static int devfs_munmap(struct vfs_node* node, void* addr, off_t offset) {
    struct devfs_node* dnode = (struct devfs_node*) node;
    if (!dnode->devops->munmap) {
        return -ENODEV;
    }

    return dnode->devops->munmap(dnode->stat.st_rdev, addr, offset);
}

static ssize_t devfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset) {
    if (node != (struct vfs_node*) devfs_root_node) {
        return -ENODEV;
    }
    if (node->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    if (offset < 0) {
        return -EINVAL;
    }

    struct devfs_node* dnode = (struct devfs_node*) node;

    ssize_t written = 0;

    if (offset <= 0 && written < (ssize_t) count) {
        struct dirent ent = {
            .d_ino = dnode->stat.st_ino,
            .d_off = 1,
            .d_reclen = sizeof(struct dirent),
            .d_type = DT_DIR,
            .d_name = ".",
        };

        int ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(ent));
        if (ret < 0) {
            return ret;
        }

        written++;
    }

    if (offset <= 1 && written < (ssize_t)count) {
        struct dirent ent = {
            .d_ino = dnode->stat.st_ino, // We are always devfs_root_node for now, so no parent
            .d_off = 2,
            .d_reclen = sizeof(struct dirent),
            .d_type = DT_DIR,
            .d_name = "..",
        };

        int ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(ent));
        if (ret < 0) {
            return ret;
        }

        written++;
    }

    size_t child_index = 0;
    size_t child_offset = offset >= 2 ? offset - 2 : 0;

    HASHMAP_FOREACH(devices) {
        if (child_index++ < child_offset) {
            continue;
        }

        if (written >= (ssize_t) count) {
            break;
        }

        struct devfs_node* child = entry->value;

        struct dirent ent = {
            .d_ino = child->stat.st_ino,
            .d_off = child_index + 1,
            .d_reclen = sizeof(struct dirent),
            .d_type = vfs_type_to_dirent(child->type),
        };

        memcpy(ent.d_name, entry->key, entry->key_size);
        ent.d_name[entry->key_size] = '\0';

        int ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(ent));
        if (ret < 0) {
            return ret;
        }

        written++;
    }

    dnode->stat.st_atim = time_realtime;
    return written;
}

static int devfs_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct devfs_node*) node)->stat, sizeof(struct stat));
}

static int devfs_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    struct devfs_node* dnode = (struct devfs_node*) node;

    if (flags & VFS_STAT_ST_ATIM) {
        dnode->stat.st_atim = stat->st_atim;
    }
    if (flags & VFS_STAT_ST_MTIM) {
        dnode->stat.st_mtim = stat->st_mtim;
    }
    if (flags & VFS_STAT_ST_CTIM) {
        dnode->stat.st_ctim = stat->st_ctim;
    }

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
    if (node != (struct vfs_node*) devfs_root_node) {
        VFS_NODE_UNREF(devfs_root_node);
    }

    slab_cache_free(devfs_node_cache, node);
}

int devfs_get(const char* name, struct vfs_node** result) {
    struct vfs_node* device = NULL;

    int ret = vfs_lookup((struct vfs_node*) devfs_root_node, name, 0, NULL, &device);
    if (ret == 0) {
        device->ops->unlock(device);
    }

    *result = device;
    return ret;
}

int devfs_register(const char* name, vfs_type_t type, struct device_ops* ops, dev_t dev) {
    if (type != VFS_TYPE_BLOCKDEV && type != VFS_TYPE_CHARDEV) {
        return -EINVAL;
    }

    struct devfs_node* node = slab_cache_alloc(devfs_node_cache);
    if (unlikely(!node)) {
        return -ENOMEM;
    }

    struct vfs_node* child;
    if (vfs_lookup((struct vfs_node*) devfs_root_node, name, 0, NULL, &child) == 0) {
        child->ops->unlock(child);
        VFS_NODE_UNREF(child);
        return -EEXIST;
    }

    node->type = type;
    node->ops = &devfs_node_ops;
    node->filesystem = devfs_root_node->filesystem;
    node->refcount = 1;

    if (ops->mmap != NULL && ops->munmap != NULL) {
        node->flags |= VFS_NODE_FLAG_MMAP;
    }

    node->stat.st_dev = 0;
    node->stat.st_ino = __atomic_add_fetch(&inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_mode = vfs_type_to_mode(type);
    node->stat.st_nlink = 1;
    node->stat.st_rdev = dev;
    node->stat.st_size = 0;
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_blocks = 0;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    node->devops = ops;

    if (unlikely(!hashmap_set(devices, name, strlen(name), node))) {
        slab_cache_free(devfs_node_cache, node);
        return -ENOMEM;
    }

    // Hold the parent node, which currently for this simple implementation is always the devfs root
    VFS_NODE_REF((struct vfs_node*) devfs_root_node);

    return 0;
}

void devfs_init(void) {
    devfs_node_cache = slab_cache_create("struct devfs_node cache", sizeof(struct devfs_node));
    if (unlikely(!devfs_node_cache)) {
        kpanic(NULL, false, "failed to create object cache for devfs nodes");
    }

    devfs_root_node = slab_cache_alloc(devfs_node_cache);
    if (unlikely(!devfs_root_node)) {
        kpanic(NULL, false, "failed to create devfs root node");
    }

    devices = hashmap_create(20);
    if (unlikely(!devices)) {
        kpanic(NULL, false, "failed to create devfs device map");
    }

    devfs_root_node->type = VFS_TYPE_DIRECTORY;
    devfs_root_node->flags = VFS_NODE_FLAG_ROOT;
    devfs_root_node->ops = &devfs_node_ops;
    devfs_root_node->refcount = 1;

    devfs_root_node->stat.st_ino = __atomic_fetch_add(&inode_counter, 1, __ATOMIC_SEQ_CST);
    devfs_root_node->stat.st_mode = vfs_type_to_mode(devfs_root_node->type);
    devfs_root_node->stat.st_nlink = 2;
    devfs_root_node->stat.st_blksize = PAGE_SIZE_4KB;
    devfs_root_node->stat.st_atim = devfs_root_node->stat.st_mtim = devfs_root_node->stat.st_ctim = time_realtime;

    if (unlikely(!vfs_register_fs("devfs", &devfs_ops))) {
        kpanic(NULL, false, "failed to register devfs with vfs");
    }
}
