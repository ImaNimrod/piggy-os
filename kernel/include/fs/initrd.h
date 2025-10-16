#ifndef _KERNEL_FS_INITRD_H
#define _KERNEL_FS_INITRD_H

#include <limine.h>

void initrd_unpack(struct limine_file* initrd_module);

#endif /* _KERNEL_FS_INITRD_H */
