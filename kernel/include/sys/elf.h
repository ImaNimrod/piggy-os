#ifndef _KERNEL_SYS_ELF_H
#define _KERNEL_SYS_ELF_H 1

#include <mem/paging.h>
#include <stdbool.h>
#include <stdint.h>

bool elf_load(struct pagemap* pagemap, uintptr_t* entry);

#endif /* _KERNEL_SYS_ELF_H */
