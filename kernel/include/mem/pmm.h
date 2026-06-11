#ifndef _KERNEL_MEM_PMM_H
#define _KERNEL_MEM_PMM_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

uintptr_t pmm_alloc(size_t page_count);
uintptr_t pmm_alloc_zero(size_t page_count);
void pmm_free(uintptr_t paddr, size_t page_count);
void pmm_reserve_mmio_space(uintptr_t paddr, size_t page_count);
void pmm_init(void);

#endif /* _KERNEL_MEM_PMM_H */
