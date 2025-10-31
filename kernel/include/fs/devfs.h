#ifndef _KERNEL_FS_DEVFS_H
#define _KERNEL_FS_DEVFS_H

#include <fs/vfs.h>
#include <stddef.h>

struct device_ops {
    ssize_t (*read)(dev_t, void*, size_t, off_t, int);
    ssize_t (*write)(dev_t, const void*, size_t, off_t, int);
    int (*ioctl)(dev_t, int, void*);
};

int devfs_register(const char* name, vfs_type_t type, struct device_ops* ops, dev_t dev);
void devfs_init(void);

#endif /* _KERNEL_FS_DEVFS_H */
