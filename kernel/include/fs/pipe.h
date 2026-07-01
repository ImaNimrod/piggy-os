#ifndef _KERNEL_FS_PIPE_H
#define _KERNEL_FS_PIPE_H

#include <fs/vfs.h>

#define FIONREAD 0x541b

int pipe_create(struct vfs_node** ret);

#endif /* _KERNEL_FS_PIPE_H */
