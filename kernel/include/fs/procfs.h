#ifndef _KERNEL_FS_PROCFS_H
#define _KERNEL_FS_PROCFS_H

#include <types.h>

void procfs_delete_nodes(pid_t pid);
void procfs_init(void);

#endif /* _KERNEL_FS_PROCFS_H */
