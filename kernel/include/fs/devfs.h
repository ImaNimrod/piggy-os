#ifndef _KERNEL_FS_DEVFS_H
#define _KERNEL_FS_DEVFS_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct vfs_node* devfs_create_device(const char* name);
void devfs_init(void);

#endif /* _KERNEL_FS_DEVFS_H */
