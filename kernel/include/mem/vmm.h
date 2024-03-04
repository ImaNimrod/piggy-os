#ifndef _KERNEL_VMM_H
#define _KERNEL_VMM_H

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIGH_VMA (hhdm_request.response->offset)
#define PAGE_SIZE 4096

#define PTE_ADDR_MASK 0x000ffffffffff000

#define PTE_PRESENT     (1ull << 0)
#define PTE_WRITABLE    (1ull << 1)
#define PTE_USER        (1ull << 2)
#define PTE_PAT         (1ull << 7)
#define PTE_GLOBAL      (1ull << 8)
#define PTE_NX          (1ull << 63)

struct pagemap {
	bool (*map_page)(struct pagemap* pagemap, uintptr_t vaddr, uint64_t paddr, uint64_t flags);
	bool (*unmap_page)(struct pagemap* pagemap, uintptr_t vaddr);

    uint64_t* top_level;
};

extern volatile struct limine_hhdm_request hhdm_request;

void vmm_init(void);
void vmm_switch_pagemap(struct pagemap* pagemap);

#endif /* _KERNEL_VMM_H */
