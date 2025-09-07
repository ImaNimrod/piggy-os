#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/log.h>
#include <utils/string.h>

struct tmpfs_node {
    struct vfs_node;
    struct stat stat;
    union {
        void* buf;
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
static int tmpfs_lock(struct vfs_node* node);
static int tmpfs_unlock(struct vfs_node* node);
static void tmpfs_inactive(struct vfs_node* node);

static struct vfs_node_ops tmpfs_node_ops = {
    .create = tmpfs_create,
    .lookup = tmpfs_lookup,
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
        node->buf = (void*) (pmm_alloc_zero(1) + HIGH_VMA);
    }

    node->type = type;
    node->ops = &tmpfs_node_ops;
    node->filesystem = filesystem;
    node->refcount = 1;

    return node;
}

static int tmpfs_mount(struct vfs_node* backing, struct vfs_node* filesystem, struct vfs_filesystem** result) {
    (void) backing;
    (void) filesystem;

    struct vfs_filesystem* vfs = kmalloc(sizeof(struct vfs_filesystem));
    if (unlikely(vfs == NULL)) {
        return -1;
    }

    *result = vfs;
    vfs->ops = &tmpfs_ops;

    return 0;
}

static int tmpfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    if (filesystem->root != NULL) {
        *result = filesystem->root;
        return 0;
    }

    struct tmpfs_node* node = create_node(filesystem, VFS_TYPE_DIRECTORY);
    if (unlikely(node == NULL)) {
        return -1;
    }

    *result = (struct vfs_node*) node;
    filesystem->root = *result;

    node->flags |= VFS_FLAG_ROOT;
    return 0;
}

int tmpfs_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -1;
    }

    size_t name_len = strlen(name);

    struct tmpfs_node* tmpfs_parent = (struct tmpfs_node*) parent;

    void* v;
    if (hashmap_get(tmpfs_parent->children, name, name_len, &v)) {
        return -1;
    }

    struct tmpfs_node* new = create_node(parent->filesystem, type);
    if (unlikely(new == NULL)) {
        return -1;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        if (!hashmap_set(new->children, "..", 2, tmpfs_parent)) {
            new->ops->unlock((struct vfs_node*) new);
            return -1;
        }
    }

    if (!hashmap_set(tmpfs_parent->children, name, name_len, new)) {
        new->ops->unlock((struct vfs_node*) new);
        return -1;
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
        return -1;
    }

    void* r;
    if (!hashmap_get(((struct tmpfs_node*) parent)->children, name, strlen(name), &r)) {
        return -1;
    }

    struct vfs_node* child = r;
    VFS_NODE_REF(child);

    if (strcmp(name, "..") == 0) {
        child->ops->unlock(parent);
    }

    if (child != parent) {
        child->ops->lock(child);
    }

    *result = child;
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
    slab_cache_free(tmpfs_node_cache, node);
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
