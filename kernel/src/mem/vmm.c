#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>

extern uintptr_t text_start_addr, text_end_addr;
extern uintptr_t rodata_start_addr, rodata_end_addr;
extern uintptr_t data_start_addr, data_end_addr;

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static struct pagemap* kernel_pagemap;
static struct cache* pagemap_cache;

void vmm_init(void) {
    pagemap_cache = cache_create("vmm_pagemap", sizeof(struct pagemap), NULL, NULL);

    kernel_pagemap = cache_alloc_object(pagemap_cache);
    kernel_pagemap->top_level = (uint64_t*) (pmm_alloc(1) + HIGH_VMA);
}
