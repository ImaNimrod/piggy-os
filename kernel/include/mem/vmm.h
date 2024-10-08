#ifndef _KERNEL_MEM_VMM_H
#define _KERNEL_MEM_VMM_H

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

#define HIGH_VMA (hhdm_request.response->offset)

#define PAGE_SIZE       0x1000
#define BIGPAGE_SIZE    0x200000

#define PTE_PRESENT         (1 << 0)
#define PTE_WRITABLE        (1 << 1)
#define PTE_USER            (1 << 2)
#define PTE_CACHE_DISABLE   (1 << 4)
#define PTE_SIZE            (1 << 7)
#define PTE_GLOBAL          (1 << 8)
#define PTE_NX              (1ul << 63)
#define PTE_FLAG_MASK       (0x8000000000000ffful)

#define FAULT_PRESENT   (1 << 0)
#define FAULT_WRITABLE  (1 << 1)
#define FAULT_USER      (1 << 2)
#define FAULT_RESERVED  (1 << 3)
#define FAULT_FETCH     (1 << 4)

#define READONLY_AFTER_INIT __attribute__((section(".ro_after_init")))
#define UNMAP_AFTER_INIT    __attribute__((noinline, section(".unmap_after_init")))

extern volatile struct limine_hhdm_request hhdm_request;
extern struct pagemap* kernel_pagemap;

struct pagemap {
    uint64_t* top_level;
    bool has_level5;
    spinlock_t lock;
};

struct pagemap* vmm_new_pagemap(void);
void vmm_destroy_pagemap(struct pagemap* pagemap);
struct pagemap* vmm_fork_pagemap(struct pagemap* old_pagemap);

void vmm_switch_pagemap(struct pagemap* pagemap);

bool vmm_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags);
bool vmm_unmap_page(struct pagemap* pagemap, uintptr_t vaddr);
bool vmm_update_flags(struct pagemap* pagemap, uintptr_t vaddr, uint64_t flags);
uintptr_t vmm_get_page_mapping(struct pagemap* pagemap, uintptr_t vaddr);

void vmm_readonly_data_after_init(void);
void vmm_unmap_text_after_init(void);

void vmm_init(void);

#endif /* _KERNEL_MEM_VMM_H */
