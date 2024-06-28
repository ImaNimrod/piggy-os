#include <fs/vfs.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/string.h>

struct path2node_res {
    struct vfs_node* parent;
    struct vfs_node* node;
};

static struct cache* vfs_node_cache;
static spinlock_t vfs_lock = {0};
static hashmap_t* vfs_filesystems;

struct vfs_node* vfs_root;

static ssize_t read_stub(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    (void) count;
    return -1;
}

static ssize_t write_stub(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) node;
    (void) buf;
    (void) offset;
    (void) count;
    return -1;
}

static int ioctl_stub(struct vfs_node* node, uint64_t request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -1; 
}

static bool truncate_stub(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return false; 
}

static void create_dotentries(struct vfs_node* parent, struct vfs_node* node) {
    struct vfs_node* dot = vfs_create_node(node->fs, node, ".", VFS_NODE_REGULAR);
    struct vfs_node* dotdot = vfs_create_node(node->fs, node, "..", VFS_NODE_REGULAR);

    dot->link = node;
    dotdot->link = parent;

    hashmap_set(node->children, ".", 1, dot);
    hashmap_set(node->children, "..", 2, dotdot);
}

static struct path2node_res path2node(struct vfs_node* parent, const char* path) {
    if (!path || strlen(path) == 0) {
        return (struct path2node_res) {0};
    }

    size_t path_len = strlen(path);
    bool ask_for_dir = path[path_len - 1] == '/';

    size_t index = 0;
    struct vfs_node* current_node = vfs_reduce_node(parent);

    if (path[index] == '/') {
        current_node = vfs_reduce_node(vfs_root);
        while (path[index] == '/') {
            if (index == path_len - 1) {
                return (struct path2node_res) { current_node, current_node };
            }

            index++;
        }
    }

    for (;;) {
        const char* elem = &path[index];
        size_t elem_len = 0;

        while (index < path_len && path[index] != '/') {
            elem_len++, index++;
        }

        while (index < path_len && path[index] == '/') {
            index++;
        }

        bool last = index == path_len;

        char* elem_str = kmalloc(elem_len + 1);
        memcpy(elem_str, elem, elem_len);

        current_node = vfs_reduce_node(current_node);

        struct vfs_node* new_node = hashmap_get(current_node->children, elem_str, strlen(elem_str));
        if (!new_node) {
            if (last) {
                return (struct path2node_res) { current_node, NULL };
            }
            return (struct path2node_res) {0};
        }

        new_node = vfs_reduce_node(new_node);
        if (last) {
            if (ask_for_dir && new_node->type != VFS_NODE_DIRECTORY) {
                return (struct path2node_res) { current_node, NULL };
            }
            return (struct path2node_res) { current_node, new_node };
        }

        current_node = new_node;

        if (current_node->type != VFS_NODE_DIRECTORY) {
            return (struct path2node_res) {0};
        }
    }

    return (struct path2node_res) {0};
}

struct vfs_node* vfs_create_node(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type) {
    struct vfs_node* node = cache_alloc_object(vfs_node_cache);
    if (node == NULL) {
        return NULL;
    }

    node->name = strdup(name);
    node->type = type;
    node->fs = fs;
    node->parent = parent;

    if (type == VFS_NODE_DIRECTORY) {
        node->children = hashmap_create(128);
    }

    node->read = read_stub;
    node->write = write_stub;
    node->ioctl = ioctl_stub;
    node->truncate = truncate_stub;

    return node;
}

struct vfs_node* vfs_reduce_node(struct vfs_node* node) {
    if (node->link) {
        return vfs_reduce_node(node->link);
    }

    if (node->mountpoint) {
        return vfs_reduce_node(node->mountpoint);
    }

    return node;
}

void vfs_destroy_node(struct vfs_node* node) {
    kfree(node->name);

    if (node->type == VFS_NODE_DIRECTORY) {
        hashmap_destroy(node->children);
    }

    cache_free_object(vfs_node_cache, node);
}

struct vfs_node* vfs_get_node(struct vfs_node* parent, const char* path) {
    spinlock_acquire(&vfs_lock);

    struct path2node_res r = path2node(parent, path);
    if (!r.node) {
        spinlock_release(&vfs_lock);
        return NULL;
    }

    spinlock_release(&vfs_lock);
    return r.node;
}

size_t vfs_get_pathname(struct vfs_node* node, char* buffer, size_t len) {
    if (node == vfs_root) {
        char* pathname = vfs_reduce_node(node)->name;
        size_t path_len = strlen(pathname);
        strncpy(buffer, pathname, len);
        return path_len;
    }

    size_t offset = 0;

    if (node->parent != vfs_root && node->parent != NULL) {
        struct vfs_node* parent = vfs_reduce_node(node->parent);

        if (parent != vfs_root && parent != NULL) {
            offset += vfs_get_pathname(parent, buffer, len - offset - 1);
            buffer[offset++] = '/';
        }
    }

    if (strcmp(node->name, "/") != 0) {
        strncpy(buffer + offset, node->name, len - offset);
        return strlen(node->name) + offset;
    }

    return offset;
}

bool vfs_mount(struct vfs_node* parent, const char* source, const char* target, const char* fs_name) {
    spinlock_acquire(&vfs_lock);

    struct path2node_res r;

    struct vfs_filesystem* fs = hashmap_get(vfs_filesystems, fs_name, strlen(fs_name));
    if (fs == NULL) {
        spinlock_release(&vfs_lock);
        return false;
    }

    struct vfs_node* source_node = NULL;
    if (source != NULL && strlen(source) != 0) {
        r = path2node(parent, source);
        source_node = r.node;
        if (source_node == NULL) {
            spinlock_release(&vfs_lock);
            return false;
        }

        if (source_node->type == VFS_NODE_DIRECTORY) {
            spinlock_release(&vfs_lock);
            return false;
        }
    }

    r = path2node(parent, target);
    if (r.node == NULL) {
        spinlock_release(&vfs_lock);
        return false;
    }

    if (!(r.node == vfs_root) && !(r.node->type == VFS_NODE_DIRECTORY)) {
        spinlock_release(&vfs_lock);
        return false;
    }

    struct vfs_node* mount_node = fs->mount(r.parent, source_node, basename((char*) target));
    if (mount_node == NULL) {
        spinlock_release(&vfs_lock);
        return false;
    }

    r.node->mountpoint = mount_node;

    create_dotentries(r.parent, mount_node);

    if (source && strlen(source) != 0) {
        klog("[vfs] mounted `%s` on `%s` with filesystem `%s`\n", source, target, fs_name);
    } else {
        klog("[vfs] mounted `%s` on `%s`\n", fs_name, target);
    }

    spinlock_release(&vfs_lock);
    return true;
}

struct vfs_node* vfs_create(struct vfs_node* parent, const char* name, int type) {
    spinlock_acquire(&vfs_lock);

    struct path2node_res r = path2node(parent, name);

    if (r.parent == NULL) {
        spinlock_release(&vfs_lock);
        return NULL;
    }

    if (r.node) {
        spinlock_release(&vfs_lock);
        return NULL;
    }

    struct vfs_filesystem* fs = r.parent->fs;
    const char* new_node_name = (const char*) basename((char*) name);
    struct vfs_node* new_node = fs->create(fs, r.parent, new_node_name, type);

    if (!hashmap_set(r.parent->children, new_node_name, strlen(new_node_name), new_node)) {
        spinlock_release(&vfs_lock);
        return NULL;
    }

    if (new_node->type == VFS_NODE_DIRECTORY) {
        create_dotentries(r.parent, new_node);
    }

    spinlock_release(&vfs_lock);
    return new_node;
}

bool vfs_register_filesystem(const char* fs_name, struct vfs_filesystem* fs) {
    spinlock_acquire(&vfs_lock);
    bool ret = hashmap_set(vfs_filesystems, fs_name, strlen(fs_name), fs);
    spinlock_release(&vfs_lock);
    return ret;
}

bool vfs_unregister_filesystem(const char* fs_name) {
    spinlock_acquire(&vfs_lock);
    bool ret = hashmap_remove(vfs_filesystems, fs_name, strlen(fs_name));
    spinlock_release(&vfs_lock);
    return ret;
}

void vfs_init(void) {
    vfs_node_cache = slab_cache_create("vfs_node cache", sizeof(struct vfs_node));
    vfs_root = vfs_create_node(NULL, NULL, "", VFS_NODE_REGULAR);
    vfs_filesystems = hashmap_create(20);

    klog("[vfs] vfs structures initialized\n");
}
