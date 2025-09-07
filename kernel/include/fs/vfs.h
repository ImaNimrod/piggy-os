#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <types.h>
#include <utils/spinlock.h>

#define PATH_MAX_LENGTH 512

#define VFS_FLAG_ROOT (1 << 0)

#define VFS_STAT_ST_DEV     (1 << 0)
#define VFS_STAT_ST_INO     (1 << 1)
#define VFS_STAT_ST_MODE    (1 << 2)
#define VFS_STAT_ST_RDEV    (1 << 3)
#define VFS_STAT_ST_SIZE    (1 << 4)
#define VFS_STAT_ST_BLKSIZE (1 << 5)
#define VFS_STAT_ST_BLOCKS  (1 << 6)
#define VFS_STAT_ST_ATIM    (1 << 7)
#define VFS_STAT_ST_MTIM    (1 << 8)
#define VFS_STAT_ST_CTIM    (1 << 9)

typedef enum {
    VFS_TYPE_REGULAR,
    VFS_TYPE_DIRECTORY,
    VFS_TYPE_BLOCKDEV,
    VFS_TYPE_CHARDEV,
} vfs_type_t;

struct vfs_filesystem;
struct vfs_node;

struct vfs_ops {
    int (*mount)(struct vfs_node*, struct vfs_node*, struct vfs_filesystem**);
    int (*unmount)(struct vfs_filesystem**);

    int (*root)(struct vfs_filesystem*, struct vfs_node**);
    int (*sync)(struct vfs_filesystem*);
};

struct vfs_node_ops {
    int (*create)(struct vfs_node*, char*, vfs_type_t, struct vfs_node**);
    int (*lookup)(struct vfs_node*, char*, struct vfs_node**);
    int (*unlink)(struct vfs_node*, char*, struct vfs_node**);

    ssize_t (*read)(struct vfs_node*, void*, size_t, off_t);
    ssize_t (*write)(struct vfs_node*, const void*, size_t, off_t);
    int (*ioctl)(struct vfs_node*, int, void*);

    int (*getstat)(struct vfs_node*, struct stat*);
    int (*setstat)(struct vfs_node*, const struct stat*, int);

    int (*lock)(struct vfs_node*);
    int (*unlock)(struct vfs_node*);
    void (*inactive)(struct vfs_node*);
};

struct vfs_filesystem {
    struct vfs_node* root;
    struct vfs_node* node;
    struct vfs_ops* ops;

    struct vfs_filesystem* next;
};

struct vfs_node {
    vfs_type_t type;
    int flags;
    struct vfs_node_ops* ops;

    struct vfs_filesystem* filesystem;
    struct vfs_filesystem* mounted;

    size_t refcount;
};

extern struct vfs_node* vfs_root;

#define VFS_NODE_REF(node) __atomic_add_fetch(&(node)->refcount, 1, __ATOMIC_SEQ_CST)
#define VFS_NODE_UNREF(node) do { \
    if (__atomic_sub_fetch(&(node)->refcount, 1, __ATOMIC_SEQ_CST) == 0) { \
        (node)->ops->inactive((node)); \
    } \
} while (0)

int vfs_mount(struct vfs_node* backing, struct vfs_node* path_reference, char* path, char* fs_name);
int vfs_create(struct vfs_node* reference, char* path, vfs_type_t type, struct vfs_node** result);
int vfs_unlink(struct vfs_node* reference, char* path);
int vfs_lookup(struct vfs_node* reference, char* path, bool lookup_parent, char* last_component, struct vfs_node** result);
bool vfs_register_fs(const char* name, struct vfs_ops* ops);
bool vfs_unregister_fs(const char* name);
void vfs_init(void);

#endif /* _KERNEL_FS_VFS_H */
