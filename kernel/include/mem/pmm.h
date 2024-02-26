#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <stddef.h>
#include <stdint.h>

void* pmm_alloc(size_t pages);
void pmm_free(void* addr, size_t pages);

void pmm_init(void);

#endif /* _KERNEL_PMM_H */
