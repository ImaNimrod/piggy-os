#ifndef _KERNEL_FS_DEVFS_H
#define _KERNEL_FS_DEVFS_H 1

#include <fs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <types.h>

struct device_ops {
    ssize_t (*read)(int, void*, size_t, off_t);
    ssize_t (*write)(int, const void*, size_t, off_t);
    int (*ioctl)(int, int, void*);
};

int devfs_register_device(const char* name, vfs_type_t type, struct device_ops* ops, int major, int minor);
void devfs_init(void);

#endif /* _KERNEL_FS_DEVFS_H */
