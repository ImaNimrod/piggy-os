#include <errno.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/timer.h>
#include <types.h>
#include <utils/hashmap.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/log.h>
#include <utils/string.h>

struct tmpfs_filesystem {
    struct vfs_filesystem;
    ino_t inode_counter;
};

struct tmpfs_node {
    struct vfs_node;
    struct stat stat;
    union {
        struct {
            void* data;
            size_t capacity;
        };
        hashmap_t* children;
    };
    spinlock_t lock;
};

static struct slab_cache* tmpfs_node_cache = NULL;

static int tmpfs_mount(struct vfs_node* backing, struct vfs_node* filesystem, struct vfs_filesystem** result);
static int tmpfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops tmpfs_ops = {
    .mount = tmpfs_mount,
    .root = tmpfs_root,
};

static int tmpfs_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result);
static int tmpfs_lookup(struct vfs_node* parent, char* name, struct vfs_node** result);
static int tmpfs_unlink(struct vfs_node* parent, char* name, struct vfs_node** result);
static ssize_t tmpfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset);
static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset);
static int tmpfs_getstat(struct vfs_node* node, struct stat* stat);
static int tmpfs_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int tmpfs_lock(struct vfs_node* node);
static int tmpfs_unlock(struct vfs_node* node);
static void tmpfs_inactive(struct vfs_node* node);

static struct vfs_node_ops tmpfs_node_ops = {
    .create = tmpfs_create,
    .lookup = tmpfs_lookup,
    .unlink = tmpfs_unlink,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .getstat = tmpfs_getstat,
    .setstat = tmpfs_setstat,
    .lock = tmpfs_lock,
    .unlock = tmpfs_unlock,
    .inactive = tmpfs_inactive,
};

static struct tmpfs_node* create_node(struct vfs_filesystem* filesystem, vfs_type_t type) {
    struct tmpfs_node* node = slab_cache_alloc(tmpfs_node_cache);
    if (unlikely(node == NULL)) {
        return NULL;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        node->children = hashmap_create(32);
        if (unlikely(node->children == NULL)) {
            slab_cache_free(tmpfs_node_cache, node);
            return NULL;
        }

        if (unlikely(!hashmap_set(node->children, ".", 1, node))) {
            hashmap_destroy(node->children);
            slab_cache_free(tmpfs_node_cache, node);
            return NULL;
        }
    } else if (type == VFS_TYPE_REGULAR) {
        node->data = (void*) (pmm_alloc_zero(1) + HIGH_VMA);
        node->capacity = PAGE_SIZE_4KB;
    }

    node->stat.st_ino = __atomic_add_fetch(&((struct tmpfs_filesystem*) filesystem)->inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_atim = node->stat.st_mtim, node->stat.st_ctim = time_realtime;

    node->type = type;
    node->ops = &tmpfs_node_ops;
    node->filesystem = filesystem;
    node->refcount = 1;

    return node;
}

static int tmpfs_mount(struct vfs_node* backing, struct vfs_node* filesystem, struct vfs_filesystem** result) {
    (void) backing;
    (void) filesystem;

    struct tmpfs_filesystem* tmpfs = kmalloc(sizeof(struct tmpfs_filesystem));
    if (unlikely(tmpfs == NULL)) {
        return -ENOMEM;
    }

    *result = (struct vfs_filesystem*) tmpfs;
    tmpfs->ops = &tmpfs_ops;

    return 0;
}

static int tmpfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    if (filesystem->root != NULL) {
        *result = filesystem->root;
        return 0;
    }

    struct tmpfs_node* node = create_node(filesystem, VFS_TYPE_DIRECTORY);
    if (unlikely(node == NULL)) {
        return -ENOMEM;
    }

    *result = (struct vfs_node*) node;
    filesystem->root = *result;

    node->flags |= VFS_FLAG_ROOT;
    return 0;
}

static int tmpfs_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    size_t name_len = strlen(name);

    struct tmpfs_node* tmpfs_parent = (struct tmpfs_node*) parent;

    void* v;
    if (hashmap_get(tmpfs_parent->children, name, name_len, &v)) {
        return -EEXIST;
    }

    struct tmpfs_node* new = create_node(parent->filesystem, type);
    if (unlikely(new == NULL)) {
        return -ENOMEM;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        if (!hashmap_set(new->children, "..", 2, tmpfs_parent)) {
            new->ops->unlock((struct vfs_node*) new);
            return -ENOMEM;
        }
    }

    if (!hashmap_set(tmpfs_parent->children, name, name_len, new)) {
        new->ops->unlock((struct vfs_node*) new);
        return -ENOMEM;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        VFS_NODE_REF(tmpfs_parent);
    }

    new->filesystem = parent->filesystem;
    *result = (struct vfs_node*) new;

    VFS_NODE_REF(new);
    new->ops->lock((struct vfs_node*) new);

    return 0;
}

static int tmpfs_lookup(struct vfs_node* parent, char* name, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    void* r;
    if (!hashmap_get(((struct tmpfs_node*) parent)->children, name, strlen(name), &r)) {
        return -ENOENT;
    }

    struct vfs_node* child = r;
    VFS_NODE_REF(child);

    if (strcmp(name, "..") == 0) {
        child->ops->unlock(parent);
    }

    if (child != parent) {
        child->ops->lock(child);
    }

    if (likely(result != NULL)) {
        *result = child;
    }
    return 0;
}

static int tmpfs_unlink(struct vfs_node* parent, char* name, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    if (parent->mounted != NULL) {
        return -EBUSY;
    }

    size_t name_len = strlen(name);

    struct tmpfs_node* child;
    if (!hashmap_get(((struct tmpfs_node*) parent)->children, name, name_len, (void**) &child)) {
        return -ENOENT;
    }

    if (child->type == VFS_TYPE_DIRECTORY) {
        if (hashmap_size(child->children) > 2) {
            return -ENOTEMPTY;
        }
    }

    if (!hashmap_remove(((struct tmpfs_node*) parent)->children, name, name_len)) {
        return -EIO;
    }

    VFS_NODE_UNREF((struct vfs_node*) child);
    if (likely(result != NULL)) {
        *result = (struct vfs_node*) child;
    }
    return 0;
}

static ssize_t tmpfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset) {
    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    size_t actual_count = count;
    if ((off_t) (offset + count) >= tnode->stat.st_size) {
        actual_count = count - ((offset + count) - tnode->stat.st_size);
    }

    memcpy(buf, (void*) ((uintptr_t) tnode->data + offset), actual_count);

    tnode->stat.st_atim = time_realtime;
    return actual_count;
}

static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset) {
    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (offset + count >= tnode->capacity) {
        size_t new_capacity = tnode->capacity;
        while (offset + count >= new_capacity) {
            new_capacity *= 2;
        }

        pmm_free((uintptr_t) tnode->data - HIGH_VMA, tnode->capacity * PAGE_SIZE_4KB);
        void* new_data = (void*) (pmm_alloc_zero(new_capacity * PAGE_SIZE_4KB) + HIGH_VMA);

        tnode->data = new_data;
        tnode->capacity = new_capacity;
    }

    memcpy((void*) ((uintptr_t) tnode->data + offset), buf, count);

    if ((off_t) (offset + count) >= tnode->stat.st_size) {
        tnode->stat.st_size = (off_t) (offset + count);
        tnode->stat.st_blocks = DIV_CEIL(tnode->stat.st_size, tnode->stat.st_blksize);
    }

    tnode->stat.st_atim = tnode->stat.st_mtim = time_realtime;
    return 0;
}

static int tmpfs_getstat(struct vfs_node* node, struct stat* stat) {
    memcpy64((void*) stat, (const void*) &((struct tmpfs_node*) node)->stat, sizeof(struct stat) >> 3);
    return 0;
}

static int tmpfs_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (flags & VFS_STAT_ST_ATIM) {
        tnode->stat.st_atim = stat->st_atim;
    }
    if (flags & VFS_STAT_ST_MTIM) {
        tnode->stat.st_mtim = stat->st_mtim;
    }
    if (flags & VFS_STAT_ST_CTIM) {
        tnode->stat.st_ctim = stat->st_ctim;
    }

    return 0;
}

static int tmpfs_lock(struct vfs_node* node) {
    spinlock_acquire(&((struct tmpfs_node*) node)->lock);
    return 0;
}

static int tmpfs_unlock(struct vfs_node* node) {
    spinlock_release(&((struct tmpfs_node*) node)->lock);
    return 0;
}

static void tmpfs_inactive(struct vfs_node* node) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (tnode->type == VFS_TYPE_REGULAR) {
        pmm_free((uintptr_t) tnode->data - HIGH_VMA, tnode->capacity / PAGE_SIZE_4KB);
    } else if (tnode->type == VFS_TYPE_DIRECTORY) {
        hashmap_destroy(tnode->children);
    }

    slab_cache_free(tmpfs_node_cache, tnode);
}

void tmpfs_init(void) {
    tmpfs_node_cache = slab_cache_create("struct tmpfs_node cache", sizeof(struct tmpfs_node));
    if (unlikely(tmpfs_node_cache == NULL)) {
        kpanic(NULL, false, "failed to create object cache for tmpfs nodes");
    }

    if (unlikely(!vfs_register_fs("tmpfs", &tmpfs_ops))) {
        kpanic(NULL, false, "failed to register tmpfs with vfs");
    }
}
