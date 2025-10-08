#ifndef _KERNEL_FS_DEVFS_H
#define _KERNEL_FS_DEVFS_H 1

#include <fs/vfs.h>
#include <stddef.h>
#include <types.h>

struct device_ops {
    ssize_t (*read)(int, void*, size_t, off_t, int);
    ssize_t (*write)(int, const void*, size_t, off_t, int);
    int (*ioctl)(int, int, void*);
};

int devfs_register_device(const char* name, vfs_type_t type, struct device_ops* ops, dev_t dev);
void devfs_init(void);

#endif /* _KERNEL_FS_DEVFS_H */
