#ifndef _KERNEL_DEV_BLOCK_PARTITION_H
#define _KERNEL_DEV_BLOCK_PARTITION_H

#include <fs/vfs.h>

#define PART_MAJ 9

void partition_scan(struct vfs_node* device_node);

#endif /* _KERNEL_DEV_BLOCK_PARTITION_H */
