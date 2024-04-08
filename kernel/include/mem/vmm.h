#ifndef _KERNEL_MEM_VMM_H
#define _KERNEL_MEM_VMM_H

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIGH_VMA (hhdm_request.response->offset)

#define PAGE_SIZE 0x1000
#define BIGPAGE_SIZE 0x200000

#define PTE_PRESENT     (1ull << 0ull)
#define PTE_WRITABLE    (1ull << 1ull)
#define PTE_USER        (1ull << 2ull)
#define PTE_SIZE        (1ull << 7ull)
#define PTE_GLOBAL      (1ull << 8ull)
#define PTE_NX          (1ull << 63ull)

extern volatile struct limine_hhdm_request hhdm_request;

struct pagemap {
    bool (*map_page)(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags);
    bool (*unmap_page)(struct pagemap* pagemap, uintptr_t vaddr);
    uint64_t* top_level;
};

struct pagemap* vmm_get_kernel_pagemap(void);
void vmm_switch_pagemap(struct pagemap* pagemap);
void vmm_init(void);

#endif /* _KERNEL_MEM_VMM_H */
