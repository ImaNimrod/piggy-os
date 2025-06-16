#ifndef _KERNEL_MEM_VMM_H
#define _KERNEL_MEM_VMM_H 1

#include <limine.h>
#include <stdbool.h>
#include <stdint.h>

extern volatile struct limine_hhdm_request hhdm_request;

#define HIGH_VMA (hhdm_request.response->offset)
#define PAGE_SIZE       (0x1000)
#define BIGPAGE_SIZE    (0x200000)

#define PTE_PRESENT         (1ul << 0)
#define PTE_WRITABLE        (1ul << 1)
#define PTE_USER            (1ul << 2)
#define PTE_CACHE_DISABLE   (1ul << 4)
#define PTE_SIZE            (1ul << 7)
#define PTE_GLOBAL          (1ul << 8)
#define PTE_NX              (1ul << 63)
#define PTE_FLAG_MASK       (0x8000000000000ffful)

struct pagemap;

extern struct pagemap* kernel_pagemap;

bool vmm_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags);
bool vmm_unmap_page(struct pagemap* pagemap, uintptr_t vaddr);
struct pagemap* vmm_create_pagemap(void);
bool vmm_destroy_pagemap(struct pagemap* pagemap);
void vmm_switch_pagemap(struct pagemap* pagemap);
void vmm_init(void);

#endif /* _KERNEL_MEM_VMM_H */
