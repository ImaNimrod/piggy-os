#ifndef _KERNEL_SYS_ELF_H
#define _KERNEL_SYS_ELF_H 1

#include <fs/vfs.h>
#include <mem/paging.h>
#include <stdint.h>

int elf_load(struct pagemap* pagemap, struct vfs_node* node, uintptr_t* entry);

#endif /* _KERNEL_SYS_ELF_H */
