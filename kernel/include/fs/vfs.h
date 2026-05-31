#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/timer.h>
#include <types.h>

#define PATH_MAX_LENGTH 512

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8

#define S_IFMT      0x0f000
#define S_IFREG     0x01000
#define S_IFDIR     0x03000
#define S_IFBLK     0x05000
#define S_IFCHR     0x07000
#define S_IFLNK     0x09000
#define S_IFIFO     0x0b000
#define S_IFSOCK    0x0d000

#define POLLIN      0x01
#define POLLOUT     0x02
#define POLLPRI     0x04
#define POLLHUP     0x08
#define POLLERR     0x10
#define POLLNVAL    0x20

#define VFS_FLAG_ROOT (1 << 0)
#define VFS_FLAG_MMAP (1 << 1)

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
    VFS_TYPE_FIFO,
} vfs_type_t;

struct vfs_filesystem;
struct vfs_node;

struct poll_table;

struct vfs_ops {
    int (*mount)(struct vfs_node*, struct vfs_node*, struct vfs_filesystem**);
    int (*unmount)(struct vfs_filesystem*);
    int (*root)(struct vfs_filesystem*, struct vfs_node**);
};

struct vfs_node_ops {
    int (*create)(struct vfs_node*, char*, vfs_type_t, struct vfs_node**);
    int (*lookup)(struct vfs_node*, char*, struct vfs_node**);
    int (*rename)(struct vfs_node*, struct vfs_node*, char*, struct vfs_node*, char*);
    int (*unlink)(struct vfs_node*, char*, struct vfs_node**);

    ssize_t (*read)(struct vfs_node*, void*, size_t, off_t, int);
    ssize_t (*write)(struct vfs_node*, const void*, size_t, off_t, int);
    int (*ioctl)(struct vfs_node*, int, void*);
    int (*truncate)(struct vfs_node*, off_t);
    short (*poll)(struct vfs_node*, short, struct poll_table*);
    int (*sync)(struct vfs_node*);
    int (*mmap)(struct vfs_node*, void*, off_t, int, uint64_t);
    int (*munmap)(struct vfs_node*, void*, off_t);
    ssize_t (*getdents)(struct vfs_node*, struct dirent*, size_t, off_t);

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
        (node)->ops->inactive((struct vfs_node*) (node)); \
        (node) = NULL; \
    } \
} while (0)

static inline unsigned char vfs_type_to_dirent(vfs_type_t type) {
    switch (type) {
        case VFS_TYPE_REGULAR:
            return DT_REG;
        case VFS_TYPE_DIRECTORY:
            return DT_DIR;
        case VFS_TYPE_BLOCKDEV:
            return DT_BLK;
        case VFS_TYPE_CHARDEV:
            return DT_CHR;
        case VFS_TYPE_FIFO:
            return DT_FIFO;
        default:
            return DT_UNKNOWN;
    }
}

static inline mode_t vfs_type_to_mode(vfs_type_t type) {
    mode_t mode = 0777;

    switch (type) {
        case VFS_TYPE_REGULAR:
            mode |= S_IFREG;
            break;
        case VFS_TYPE_DIRECTORY:
            mode |= S_IFDIR;
            break;
        case VFS_TYPE_BLOCKDEV:
            mode |= S_IFBLK;
            break;
        case VFS_TYPE_CHARDEV:
            mode |= S_IFCHR;
            break;
        case VFS_TYPE_FIFO:
            mode |= S_IFIFO;
            break;
    }

    return mode;
}

int vfs_mount(struct vfs_node* source, struct vfs_node* target_reference, const char* target_path, const char* fs_name);
int vfs_unmount(struct vfs_node* target_reference, const char* target_path);
int vfs_create(struct vfs_node* reference, const char* path, vfs_type_t type, struct vfs_node** result);
int vfs_rename(struct vfs_node* src, const char* src_path, struct vfs_node* dest, const char* dest_path);
int vfs_unlink(struct vfs_node* reference, const char* path);
int vfs_lookup(struct vfs_node* reference, const char* path, bool lookup_parent, char* last_component, struct vfs_node** result);
bool vfs_register_fs(const char* name, struct vfs_ops* ops);
bool vfs_unregister_fs(const char* name);
void vfs_init(void);

#endif /* _KERNEL_FS_VFS_H */
