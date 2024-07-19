#ifndef _KERNEL_FS_DEVFS_H
#define _KERNEL_FS_DEVFS_H

#include <fs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct device {
    char name[20];
    mode_t mode;
    void* private;
    ssize_t (*read)(struct vfs_node*, void*, off_t, size_t);
    ssize_t (*write)(struct vfs_node*, const void*, off_t, size_t);
    int (*ioctl)(struct vfs_node*, uint64_t, void*);
};

bool devfs_add_device(struct device* device);
void devfs_init(void);

#endif /* _KERNEL_FS_DEVFS_H */
