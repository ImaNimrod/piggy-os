#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <utils/hashmap.h>
#include <utils/spinlock.h>

#define PATH_MAX 4096

#define VFS_NODE_REGULAR    0
#define VFS_NODE_DIRECTORY  1

struct vfs_filesystem {
    struct vfs_node *(*mount)(struct vfs_node*, struct vfs_node*, const char*);
    struct vfs_node *(*create)(struct vfs_filesystem*, struct vfs_node*, const char*, int);
};

struct vfs_node {
    char* name;
    int type;
    ssize_t size;
    size_t refcount;
    struct vfs_node* parent;
    struct vfs_node* mountpoint;
    struct vfs_node* link;
    hashmap_t* children;
    struct vfs_filesystem* fs;
    void* device;
    spinlock_t lock;

    ssize_t (*read)(struct vfs_node*, void*, off_t, size_t);
    ssize_t (*write)(struct vfs_node*, const void*, off_t, size_t);
    int (*ioctl)(struct vfs_node*, uint64_t, void*);
    bool (*truncate)(struct vfs_node*, off_t);
};

struct file_descriptor {
    struct vfs_node* node;
    int flags;
    off_t offset;
    size_t refcount;
    spinlock_t lock;
};

struct vfs_node* vfs_create_node(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type);
struct vfs_node* vfs_reduce_node(struct vfs_node* node);
void vfs_destroy_node(struct vfs_node* node);
struct vfs_node* vfs_get_node(struct vfs_node* parent, const char* path);
size_t vfs_get_pathname(struct vfs_node* node, char* buffer, size_t len);
struct vfs_node* vfs_get_root(void);
bool vfs_mount(struct vfs_node* parent, const char* source, const char* target, const char* fs_name);
struct vfs_node* vfs_create(struct vfs_node* parent, const char* name, int type);
bool vfs_register_filesystem(const char* fs_name, struct vfs_filesystem* fs);
bool vfs_unregister_filesystem(const char* fs_name);
void vfs_init(void);

#endif /* _KERNEL_FS_VFS_H */
