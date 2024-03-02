#ifndef _KERNEL_VMM_H
#define _KERNEL_VMM_H

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIGH_VMA (hhdm_request.response->offset)
#define PAGE_SIZE 4096

struct pagemap {
	bool (*map_page)(struct pagemap* pagemap, uintptr_t vaddr, uint64_t paddr, uint64_t flags);
	bool (*unmap_page)(struct pagemap* pagemap, uintptr_t vaddr);
	bool (*lowest_level)(struct pagemap* pagemap, uintptr_t vaddr);

    uint64_t* top_level;
};

extern volatile struct limine_hhdm_request hhdm_request;

void vmm_init(void);

#endif /* _KERNEL_VMM_H */
