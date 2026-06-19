#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

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

static struct vfs_node* mount_bottom(struct vfs_node* node) {
    while (node->flags & VFS_NODE_FLAG_ROOT) {
        node = node->filesystem->node;
    }

    return node;
}

static int mount_top(struct vfs_node* node, struct vfs_node** result) {
    int ret = 0;
    while (ret == 0 && node->mounted) {
        ret = node->mounted->ops->root(node->mounted, &node);
    }

    *result = node;
    return ret;
}

int vfs_mount(struct vfs_node* source, struct vfs_node* target_reference, const char* target_path, const char* fs_name) {
    struct vfs_ops* fs_ops;
    if (!hashmap_get(vfs_filesystems, fs_name, strlen(fs_name), (void**) &fs_ops)) {
        return -EINVAL;
    }

    struct vfs_node* target;

    int error = vfs_lookup(target_reference, target_path, 0, NULL, &target);
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

    int error = vfs_lookup(target_reference, target_path, 0, NULL, &target);
    if (error < 0) {
        return error;
    }

    if (!target->mounted) {
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
    char* component = kmallocz(strlen(path) + 1);
    if (unlikely(!component)) {
        return -ENOMEM;
    }

    struct vfs_node* parent;

    int error = vfs_lookup(reference, path, VFS_LOOKUP_FLAG_PARENT, component, &parent);
    if (error < 0) {
        goto cleanup;
    }

    struct vfs_node* new_node;

    error = parent->ops->create(parent, component, type, &new_node);
    if (error < 0) {
        goto cleanup_parent;
    }

    if (result) {
        *result = new_node;
    } else {
        new_node->ops->unlock(new_node);
        VFS_NODE_UNREF(new_node);
    }

cleanup_parent:
    parent->ops->unlock(parent);
    VFS_NODE_UNREF(parent);

cleanup:
    kfree(component);
    return error;
}

int vfs_lookup(struct vfs_node* reference, const char* path, int flags, char* last_component, struct vfs_node** result) {
    if (unlikely(!path || *path == '\0')) {
        return -ENOENT;
    }

    size_t path_len = strlen(path);
    if (path_len > PATH_MAX_LENGTH) {
        return -ENAMETOOLONG;
    }

    struct vfs_node* current = reference;

    int error = mount_top(reference, &current);
    if (error < 0) {
        return error;
    }

    char* comp_buffer = strdup(path);
    if (unlikely(!comp_buffer)) {
        return -ENOMEM;
    }

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

        if (is_last && (flags & VFS_LOOKUP_FLAG_PARENT)) {
            memcpy(last_component, component, comp_len);
            last_component[comp_len] = '\0';
            break;
        }

        if (strcmp(component, ".") == 0) {
            i += comp_len;
            continue;
        }

        bool is_dotdot = strcmp(component, "..") == 0;
        if (is_dotdot) {
            struct vfs_node* root = NULL;
            mount_top(vfs_root, &root);

            if (root == current) {
                i += comp_len;
                continue;
            }

            if (current->flags & VFS_NODE_FLAG_ROOT) {
                struct vfs_node* low = mount_bottom(current);
                if (low != current) {
                    VFS_NODE_REF(low);
                    current->ops->unlock(current);
                    VFS_NODE_UNREF(current);

                    current = low;
                    current->ops->lock(current);
                }
            } else {
                error = current->ops->parent(current, &next);
                if (error < 0) {
                    break;
                }

                current->ops->unlock(current);
                VFS_NODE_UNREF(current);

                current = next;
                current->ops->lock(current);
            }

            i += comp_len;
            continue;
        }

        error = current->ops->lookup(current, component, &next);
        if (error < 0) {
            break;
        }

        if (current != next && !is_dotdot) {
            current->ops->unlock(current);
        }

        struct vfs_node* r = next;

        error = mount_top(next, &r);
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

        if (next->type == VFS_TYPE_SYMLINK && (!is_last || (is_last && !(flags & VFS_LOOKUP_FLAG_NOFOLLOW)))) {
            char link_path[PATH_MAX_LENGTH];

            ssize_t nread = next->ops->readlink(next, link_path, PATH_MAX_LENGTH - 1);
            next->ops->unlock(next);

            if (nread < 0) {
                VFS_NODE_UNREF(next);
                break;
            }

            link_path[nread] = '\0';

            struct vfs_node* link_reference = current;
            if (link_path[0] == '/') {
                link_reference = process_get_root(this_cpu()->scheduler.current_thread->process);
            }

            struct vfs_node* link_target = NULL;
            error = vfs_lookup(link_reference, link_path, 0, NULL, &link_target);

            if (link_path[0] == '/') {
                VFS_NODE_UNREF(link_reference);
            }

            VFS_NODE_UNREF(next);

            if (error < 0) {
                break;
            }

            next = link_target;
        }

        VFS_NODE_UNREF(current);
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

int vfs_rename(struct vfs_node* src_reference, const char* src_path, struct vfs_node* dest_reference, const char* dest_path) {
    char* src_component = kmallocz(strlen(src_path) + 1);
    if (unlikely(!src_component)) {
        return -ENOMEM;
    }

    char* dest_component = kmallocz(strlen(dest_path) + 1);
    if (unlikely(!dest_component)) {
        kfree(src_component);
        return -ENOMEM;
    }

    struct vfs_node* src_dir;
    struct vfs_node* dest_dir;

    int error = vfs_lookup(src_reference, src_path, VFS_LOOKUP_FLAG_PARENT | VFS_LOOKUP_FLAG_NOFOLLOW, src_component, &src_dir);
    if (error < 0) {
        goto cleanup_components;
    }

    src_dir->ops->unlock(src_dir);

    error = vfs_lookup(dest_reference, dest_path, VFS_LOOKUP_FLAG_PARENT | VFS_LOOKUP_FLAG_NOFOLLOW, dest_component, &dest_dir);
    if (error < 0) {
        VFS_NODE_UNREF(src_dir);
        goto cleanup_components;
    }

    if (src_dir != dest_dir) {
        src_dir->ops->lock(src_dir);
    }

    if (strcmp(src_component, ".") == 0 || strcmp(src_component, "..") == 0) {
        error = -EBUSY;
        goto cleanup_nodes;
    }

    if (strcmp(dest_component, ".") == 0 || strcmp(dest_component, "..") == 0) {
        error = -EEXIST;
        goto cleanup_nodes;
    }

    struct vfs_node* src;
    error = src_dir->ops->lookup(src_dir, src_component, &src);
    if (error < 0) {
        goto cleanup_nodes;
    }

    error = src_dir->ops->rename(src_dir, src, src_component, dest_dir, dest_component);

    src->ops->unlock(src);
    VFS_NODE_UNREF(src);

cleanup_nodes:
    if (src_dir != dest_dir) {
        src_dir->ops->unlock(src_dir);
    }

    dest_dir->ops->unlock(dest_dir);
    VFS_NODE_UNREF(src_dir);
    VFS_NODE_UNREF(dest_dir);

cleanup_components:
    kfree(src_component);
    kfree(dest_component);

    return error;
}

int vfs_link(struct vfs_node* dest_reference, const char* dest_path, struct vfs_node* link_reference, const char* link_path) {
    struct vfs_node* dest_node;

    int error = vfs_lookup(dest_reference, dest_path, 0, NULL, &dest_node);
    if (error < 0) {
        return error;
    }

    char* component = kmallocz(strlen(link_path) + 1);
    if (unlikely(!component)) {
        error = -ENOMEM;
        goto cleanup;
    }

    struct vfs_node* parent;

    error = vfs_lookup(link_reference, link_path, VFS_LOOKUP_FLAG_PARENT, component, &parent);
    if (error < 0) {
        kfree(component);
        goto cleanup;
    }

    error = parent->ops->link(parent, component, dest_node);

    parent->ops->unlock(parent);
    VFS_NODE_UNREF(parent);

cleanup:
    dest_node->ops->unlock(dest_node);
    VFS_NODE_UNREF(dest_node);

    return error;
}

int vfs_symlink(struct vfs_node* link_reference, const char* link_path, const char* target_path) {
    char* component = kmallocz(strlen(link_path) + 1);
    if (unlikely(!component)) {
        return -ENOMEM;
    }

    struct vfs_node* parent;

    int error = vfs_lookup(link_reference, link_path, VFS_LOOKUP_FLAG_PARENT, component, &parent);
    if (error < 0) {
        goto cleanup;
    }

    error = parent->ops->symlink(parent, component, target_path);

    parent->ops->unlock(parent);
    VFS_NODE_UNREF(parent);

cleanup:
    kfree(component);
    return error;
}

int vfs_unlink(struct vfs_node* reference, const char* path) {
    char* component = kmallocz(strlen(path) + 1);
    if (unlikely(!component)) {
        return -ENOMEM;
    }

    struct vfs_node* parent;

    int error = vfs_lookup(reference, path, VFS_LOOKUP_FLAG_PARENT | VFS_LOOKUP_FLAG_NOFOLLOW, component, &parent);
    if (error < 0) {
        goto cleanup;
    }

    if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
        parent->ops->unlock(parent);
        VFS_NODE_UNREF(parent);
        error = -EBUSY;
        goto cleanup;
    }

    struct vfs_node* child;

    error = parent->ops->lookup(parent, component, &child);
    if (error < 0) {
        parent->ops->unlock(parent);
        VFS_NODE_UNREF(parent);
        goto cleanup;
    }

    error = parent->ops->unlink(parent, child, component);

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
    if (unlikely(!vfs_filesystems)) {
        kpanic(NULL, false, "failed to create VFS filesystem map");
    }

    vfs_root = kmallocz(sizeof(struct vfs_node));
    if (unlikely(!vfs_root)) {
        kpanic(NULL, false, "failed to allocate memory for VFS root node");
    }
    vfs_root->type = VFS_TYPE_DIRECTORY;
    vfs_root->ops = &root_node_ops;
    vfs_root->refcount = 1;
}
