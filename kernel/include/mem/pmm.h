#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

extern volatile struct limine_memmap_request memmap_request;

uintptr_t pmm_alloc(size_t pages);
void pmm_free(uintptr_t addr, size_t pages);
void pmm_init(void);

#endif /* _KERNEL_PMM_H */
