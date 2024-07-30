#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <utils/hashmap.h>
#include <utils/spinlock.h>

extern struct vfs_node* vfs_root;

struct vfs_filesystem {
    struct vfs_node *(*mount)(struct vfs_node*, struct vfs_node*, const char*);
    struct vfs_node *(*create)(struct vfs_filesystem*, struct vfs_node*, const char*, mode_t);
    struct vfs_node *(*unlink)(struct vfs_node*, const char*);
};

struct vfs_node {
    char* name;
    struct stat stat;
    size_t refcount;
    struct vfs_node* parent;
    struct vfs_node* mountpoint;
    struct vfs_node* link;
    hashmap_t* children;
    struct vfs_filesystem* fs;
    void* private;
    spinlock_t lock;

    ssize_t (*read)(struct vfs_node*, void*, off_t, size_t, int);
    ssize_t (*write)(struct vfs_node*, const void*, off_t, size_t, int);
    int (*ioctl)(struct vfs_node*, uint64_t, void*);
    int (*truncate)(struct vfs_node*, off_t);
};

struct vfs_node* vfs_create_node(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, bool is_dir);
struct vfs_node* vfs_reduce_node(struct vfs_node* node);
void vfs_destroy_node(struct vfs_node* node);
struct vfs_node* vfs_get_node(struct vfs_node* parent, const char* path);
size_t vfs_get_pathname(struct vfs_node* node, char* buffer, size_t len);
struct vfs_node* vfs_get_root(void);
bool vfs_mount(struct vfs_node* parent, const char* source, const char* target, const char* fs_name);
struct vfs_node* vfs_create(struct vfs_node* parent, const char* name, mode_t mode);
bool vfs_register_filesystem(const char* fs_name, struct vfs_filesystem* fs);
bool vfs_unregister_filesystem(const char* fs_name);
void vfs_init(void);

#endif /* _KERNEL_FS_VFS_H */
