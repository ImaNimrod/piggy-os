#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

// TODO: Fix .. directory entries

struct vfs_node* vfs_root;

static hashmap_t* vfs_filesystems;

static int nop(struct vfs_node* node);

static struct vfs_node_ops root_node_ops = {
    .lock = nop,
    .unlock = nop,
};

static int nop(struct vfs_node* node) {
    (void) node;
    return 0;
}

int vfs_mount(struct vfs_node* source, struct vfs_node* target_reference, const char* target_path, const char* fs_name) {
    struct vfs_ops* fs_ops;

    if (!hashmap_get(vfs_filesystems, fs_name, strlen(fs_name), (void**) &fs_ops)) {
        return -EINVAL;
    }

    struct vfs_node* target;

    int error = vfs_lookup(target_reference, target_path, false, NULL, &target);
    if (error < 0) {
        return error;
    }

    if (target->type != VFS_TYPE_DIRECTORY) {
        target->ops->unlock(target);
        VFS_NODE_UNREF(target);
        return -ENOTDIR;
    }

    struct vfs_filesystem* filesystem;

    error = fs_ops->mount(source, target, &filesystem);
    if (error < 0) {
        target->ops->unlock(target);
        VFS_NODE_UNREF(target);
        return error;
    }

    target->mounted = filesystem;
    filesystem->node = target;

    target->ops->unlock(target);
    return 0;
}

// TODO: Make this not be terrible
int vfs_unmount(struct vfs_node* target_reference, const char* target_path) {
    struct vfs_node* target;

    int error = vfs_lookup(target_reference, target_path, false, NULL, &target);
    if (error < 0) {
        return error;
    }

    if (target->mounted == NULL) {
        error = -EINVAL;
        goto end;
    }

    error = target->mounted->ops->unmount(target->mounted);
    if (error < 0) {
        goto end;
    }

    VFS_NODE_UNREF(target);

end:
    target->ops->unlock(target);
    VFS_NODE_UNREF(target);
    return error;
}

int vfs_create(struct vfs_node* reference, const char* path, vfs_type_t type, struct vfs_node** result) {
    char* component = kmalloc(strlen(path) + 1);
    if (unlikely(component == NULL)) {
        return -ENOMEM;
    }

    struct vfs_node* parent;

    int error = vfs_lookup(reference, path, true, component, &parent);
    if (error < 0) {
        goto cleanup;
    }

    struct vfs_node* new_node;
    error = parent->ops->create(parent, component, type, &new_node);

    VFS_NODE_UNREF(parent);

    if (error < 0) {
        parent->ops->unlock(parent);
        goto cleanup;
    }

    if (result != NULL) {
        *result = new_node;
    } else {
        new_node->ops->unlock(new_node);
        VFS_NODE_UNREF(new_node);
    }

    parent->ops->unlock(parent);

cleanup:
    kfree(component);
    return error;
}

int vfs_lookup(struct vfs_node* reference, const char* path, bool lookup_parent, char* last_component, struct vfs_node** result) {
    if (unlikely(path == NULL || *path == '\0')) {
        return -ENOENT;
    }

    size_t path_len = strlen(path);
    if (path_len > PATH_MAX_LENGTH) {
        return -ENAMETOOLONG;
    }

    struct vfs_node* current = reference;

    int error = 0;
    while (error == 0 && current->mounted != NULL) {
        error = current->mounted->ops->root(current->mounted, &current);
    }
    if (error < 0) {
        return error;
    }

    char* comp_buffer = kmalloc(path_len + 1);
    if (unlikely(comp_buffer == NULL)) {
        return -ENOMEM;
    }

    strncpy(comp_buffer, path, path_len);

    for (size_t i = 0; i < path_len; i++) {
        if (comp_buffer[i] == '/') {
            comp_buffer[i] = '\0';
        }
    }

    struct vfs_node* next;

    VFS_NODE_REF(current);
    current->ops->lock(current);

    for (size_t i = 0; i < path_len; i++) {
        if (comp_buffer[i] == '\0') {
            continue;
        }

        if (current->type != VFS_TYPE_DIRECTORY) {
            error = -ENOTDIR;
            break;
        }

        char* component = &comp_buffer[i];
        size_t comp_len = strlen(component);

        bool is_last = i + comp_len == path_len;
        if (!is_last) {
            volatile size_t j;
            for (j = i + comp_len; j < path_len && comp_buffer[j] == '\0'; j++) {}
            is_last = j == path_len;
        }

        if (is_last && lookup_parent) {
            strncpy(last_component, component, comp_len);
            break;
        }

        bool is_dotdot = strcmp(component, "..") == 0;
        if (is_dotdot) {
            struct vfs_node* root = vfs_root;
            while (error == 0 && root->mounted != NULL) {
                error = vfs_root->mounted->ops->root(root->mounted, &root);
            }
            if (error < 0) {
                break;
            }

            if (root == current) {
                i += comp_len;
                continue;
            }

            if (current->flags & VFS_FLAG_ROOT) {
                struct vfs_node* low = current;
                while (low->flags & VFS_FLAG_ROOT) {
                    low = low->filesystem->node;
                }

                if (low != current) {
                    VFS_NODE_REF(low);
                    current->ops->unlock(current);
                    VFS_NODE_UNREF(current);
                    current = low;
                    current->ops->lock(current);
                }
            }
        }

        error = current->ops->lookup(current, component, &next);
        if (error < 0) {
            break;
        }

        if (current != next && !is_dotdot) {
            current->ops->unlock(current);
        }

        struct vfs_node* r = next;
        while (error == 0 && r->mounted != NULL) {
            error = r->mounted->ops->root(r->mounted, &r);
        }

        if (error < 0) {
            if (current != next) {
                next->ops->unlock(next);
            }
            VFS_NODE_UNREF(next);
            break;
        }

        if (r != next) {
            VFS_NODE_REF(r);
            next->ops->unlock(next);
            VFS_NODE_UNREF(next);
            next = r;
            next->ops->lock(next);
        }

        current->ops->unlock(current);
        current = next;
        i += comp_len;
    }

    if (error < 0) {
        current->ops->unlock(current);
        VFS_NODE_UNREF(current);
    } else {
        *result = current;
    }

    kfree(comp_buffer);
    return error;
} 

int vfs_unlink(struct vfs_node* reference, const char* path) {
    char* component = kmalloc(strlen(path) + 1);
    if (unlikely(component == NULL)) {
        return -ENOMEM;
    }

    struct vfs_node* parent;

    int error = vfs_lookup(reference, path, true, component, &parent);
    if (error < 0) {
        goto cleanup;
    }

    if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
        parent->ops->unlock(parent);
        VFS_NODE_UNREF(parent);
        error = -EBUSY;
        goto cleanup;
    }

    struct vfs_node* child = NULL;

    error = parent->ops->lookup(parent, component, &child);
    if (error < 0) {
        parent->ops->unlock(parent);
        VFS_NODE_UNREF(parent);
        goto cleanup;
    }

    error = parent->ops->unlink(parent, component, &child);

    child->ops->unlock(child);
    parent->ops->unlock(parent);

    VFS_NODE_UNREF(child);
    VFS_NODE_UNREF(parent);

cleanup:
    kfree(component);
    return error;
}

bool vfs_register_fs(const char* name, struct vfs_ops* ops) {
    return hashmap_set(vfs_filesystems, name, strlen(name), ops);
}

bool vfs_unregister_fs(const char* name) {
    return hashmap_remove(vfs_filesystems, name, strlen(name));
}

void vfs_init(void) {
    klog("[vfs] VFS subsystem initialized\n");

    vfs_filesystems = hashmap_create(10);
    if (unlikely(vfs_filesystems == NULL)) {
        kpanic(NULL, false, "failed to create VFS filesystem map");
    }

    vfs_root = kmalloc(sizeof(struct vfs_node));
    if (unlikely(vfs_root == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for VFS root node");
    }

    vfs_root->type = VFS_TYPE_DIRECTORY;
    vfs_root->ops = &root_node_ops;
    vfs_root->refcount = 1;
}
