#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/panic.h>
#include <utils/string.h>

struct path2node_res {
    struct vfs_node* parent;
    struct vfs_node* node;
};

static struct cache* vfs_node_cache;
static hashmap_t* vfs_filesystems;

struct vfs_node* vfs_root;

static ssize_t read_stub(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) node;
    (void) buf;
    (void) offset;
    (void) count;
    (void) flags;
    return -EPERM;
}

static ssize_t write_stub(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) node;
    (void) buf;
    (void) offset;
    (void) count;
    (void) flags;
    return -EPERM;
}

static int ioctl_stub(struct vfs_node* node, uint64_t request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -ENOTTY; 
}

static int truncate_stub(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EPERM; 
}

static void create_dotentries(struct vfs_node* parent, struct vfs_node* node) {
    struct vfs_node* dot = vfs_create_node(node->fs, node, ".", false);
    struct vfs_node* dotdot = vfs_create_node(node->fs, node, "..", false);

    dot->link = node;
    dotdot->link = parent;

    hashmap_set(node->children, ".", 1, dot);
    hashmap_set(node->children, "..", 2, dotdot);
}

static struct path2node_res path2node(struct vfs_node* parent, const char* path) {
    if (!path || *path == '\0') {
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
            if (ask_for_dir && !S_ISDIR(new_node->stat.st_mode)) {
                return (struct path2node_res) { current_node, NULL };
            }
            return (struct path2node_res) { current_node, new_node };
        }

        current_node = new_node;

        if (!S_ISDIR(current_node->stat.st_mode)) {
            return (struct path2node_res) {0};
        }
    }

    return (struct path2node_res) {0};
}

struct vfs_node* vfs_create_node(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, bool is_dir) {
    struct vfs_node* node = cache_alloc_object(vfs_node_cache);
    if (node == NULL) {
        return NULL;
    }

    node->fs = fs;
    node->parent = parent;
    node->name = strdup(name);

    if (is_dir) {
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

    if (S_ISDIR(node->stat.st_mode)) {
        hashmap_destroy(node->children);
    }

    cache_free_object(vfs_node_cache, node);
}

struct vfs_node* vfs_get_node(struct vfs_node* parent, const char* path) {
    struct path2node_res r = path2node(parent, path);
    if (!r.node) {
        return NULL;
    }
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
    struct path2node_res r;

    vfs_mount_t fs_mount = (vfs_mount_t) hashmap_get(vfs_filesystems, fs_name, strlen(fs_name));
    if (fs_mount == NULL) {
        return false;
    }

    struct vfs_node* source_node = NULL;
    if (source != NULL && *source != '\0') {
        r = path2node(parent, source);
        source_node = r.node;
        if (source_node == NULL) {
            return false;
        }

        if (S_ISDIR(source_node->stat.st_mode)) {
            return false;
        }
    }

    r = path2node(parent, target);
    if (r.node == NULL) {
        return false;
    }

    if (r.node != vfs_root && !S_ISDIR(r.node->stat.st_mode)) {
        return false;
    }

    struct vfs_node* mount_node = fs_mount(r.parent, source_node, basename((char*) target));
    if (mount_node == NULL) {
        return false;
    }
    r.node->mountpoint = mount_node;

    create_dotentries(r.parent, mount_node);

    return true;
}

struct vfs_node* vfs_create(struct vfs_node* parent, const char* name, mode_t mode) {
    struct path2node_res r = path2node(parent, name);

    if (r.parent == NULL) {
        return NULL;
    }

    if (r.node) {
        return NULL;
    }

    struct vfs_filesystem* fs = r.parent->fs;

    spinlock_acquire(&parent->lock);

    const char* new_node_name = (const char*) basename((char*) name);
    struct vfs_node* new_node = fs->create(fs, r.parent, new_node_name, mode);

    if (!hashmap_set(r.parent->children, new_node_name, strlen(new_node_name), new_node)) {
        spinlock_release(&parent->lock);
        return NULL;
    }

    if (S_ISDIR(new_node->stat.st_mode)) {
        create_dotentries(r.parent, new_node);
    }

    spinlock_release(&parent->lock);
    return new_node;
}

ssize_t vfs_getdents(struct vfs_node* node, struct dirent* buffer, off_t offset, size_t count) {
    if (!S_ISDIR(node->stat.st_mode)) {
        return -ENOTDIR;
    }

    size_t total_length = 0;
    off_t iter_offset = 0;

    if (node->children->entries != NULL) {
        for (size_t i = 0; i < node->children->size; i++) {
            hashmap_entry_t* entry = node->children->entries[i];
            while (entry != NULL) {
                struct vfs_node* child = entry->value;
                size_t ent_len = sizeof(struct dirent) + strlen(child->name) + 1;

                iter_offset += ent_len;
                if (iter_offset > offset) {
                    total_length += ent_len;
                }

                entry = entry->next;
            }
        }
    }

    if (total_length == 0) {
        return 0;
    }

    ssize_t actual_count = (ssize_t) MIN(total_length, count);

    ssize_t read_size = 0;
    iter_offset = 0;

    if (node->children->entries != NULL) {
        for (size_t i = 0; i < node->children->size; i++) {
            hashmap_entry_t* entry = node->children->entries[i];
            while (entry != NULL) {
                struct vfs_node* child = entry->value;
                struct vfs_node* reduced_child = vfs_reduce_node(child);

                size_t name_len = strlen(child->name) + 1;
                size_t ent_len = sizeof(struct dirent) + name_len;

                iter_offset += ent_len;
                if (iter_offset <= offset) {
                    entry = entry->next;
                    continue;
                }

                struct dirent* ent = (struct dirent*) ((uintptr_t) buffer + read_size);
                ent->d_ino = reduced_child->stat.st_ino;
                ent->d_reclen = ent_len;

                switch (reduced_child->stat.st_mode & S_IFMT) {
                    case S_IFREG:
                        ent->d_type = DT_REG;
                        break;
                    case S_IFDIR:
                        ent->d_type = DT_DIR;
                        break;
                    case S_IFCHR:
                        ent->d_type = DT_CHR;
                        break;
                    case S_IFBLK:
                        ent->d_type = DT_BLK;
                        break;
                    default:
                        ent->d_type = DT_UNKNOWN;
                        break;
                }

                memcpy(ent->d_name, child->name, name_len);

                if (read_size >= actual_count) {
                    break;
                }
                read_size += ent_len;
                entry = entry->next;
            }
        }
    }

    node->stat.st_atim = time_realtime;

    return read_size;
}

bool vfs_register_filesystem(const char* fs_name, vfs_mount_t fs_mount) {
    return hashmap_set(vfs_filesystems, fs_name, strlen(fs_name), (void*) fs_mount);
}

bool vfs_unregister_filesystem(const char* fs_name) {
    return hashmap_remove(vfs_filesystems, fs_name, strlen(fs_name));
}

void vfs_init(void) {
    vfs_node_cache = slab_cache_create("vfs_node cache", sizeof(struct vfs_node));
    if (vfs_node_cache == NULL) {
        kpanic(NULL, false, "failed to initialize object cache for vfs nodes");
    }

    vfs_root = vfs_create_node(NULL, NULL, "", false);
    vfs_filesystems = hashmap_create(20);

    klog("[vfs] vfs structures initialized\n");
}
