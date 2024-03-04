#include <cpuid.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/math.h>

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

#define DEFAULT_PTE_FLAGS (PTE_GLOBAL | PTE_NX)

static struct pagemap* kernel_pagemap;
static struct cache* pagemap_cache;

static uint64_t* get_next_level(uint64_t* top_level, size_t index, bool allocate) {
    if (top_level[index] & PTE_PRESENT) {
        return (uint64_t*) ((top_level[index] & PTE_ADDR_MASK) + HIGH_VMA);
    }

    if (!allocate) {
        return NULL;
    }

    uintptr_t next_level = pmm_alloc(1);

    top_level[index] = next_level | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return (uint64_t*) (next_level + HIGH_VMA);
}

static bool pml4_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    size_t pml4_entry = (vaddr >> 39) & 0x1ff;
    size_t pml3_entry = (vaddr >> 30) & 0x1ff;
    size_t pml2_entry = (vaddr >> 21) & 0x1ff;
    size_t pml1_entry = (vaddr >> 12) & 0x1ff;

	if (pagemap->top_level[pml4_entry] & PTE_PRESENT) {
		pagemap->top_level[pml4_entry] = pmm_alloc(1) | (flags & DEFAULT_PTE_FLAGS) | PTE_WRITABLE;
	}

    klog("test1\n");

    uint64_t* pml3 = (uint64_t*) ((pagemap->top_level[pml4_entry] & ~(0xfff)) + HIGH_VMA);
	if (!(pml3[pml3_entry] & PTE_PRESENT)) {
		pml3[pml3_entry] = pmm_alloc(1) | (flags & DEFAULT_PTE_FLAGS) | PTE_WRITABLE;
	}

    klog("test2\n");

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_entry] & ~(0xfff)) + HIGH_VMA);
    klog("0x%x\n", pml2[pml2_entry]);
    for (;;) __asm__ volatile("hlt");
	if (!(pml2[pml2_entry] & PTE_PRESENT)) {
		pml2[pml2_entry] = pmm_alloc(1) | (flags & DEFAULT_PTE_FLAGS) | PTE_WRITABLE;
	}

    klog("test3\n");

	uint64_t* pml1 = (uint64_t*) ((pml2[pml2_entry] & ~(0xfff)) + HIGH_VMA);
    pml1[pml1_entry] = paddr | flags;

    klog("test4\n");

    return true;
}

static bool pml4_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    bool success = false;

    size_t pml4_entry = (vaddr >> 39) & 0x1ff;
    size_t pml3_entry = (vaddr >> 30) & 0x1ff;
    size_t pml2_entry = (vaddr >> 21) & 0x1ff;
    size_t pml1_entry = (vaddr >> 12) & 0x1ff;

    uint64_t* pml4 = pagemap->top_level;

    uint64_t* pml3 = get_next_level(pml4, pml4_entry, false);
    if (!pml3) {
        return success;
    }

    uint64_t* pml2 = get_next_level(pml3, pml3_entry, false);
    if (!pml2) {
        return success;
    }

    uint64_t* pml1 = get_next_level(pml2, pml2_entry, false);
    if (!pml1) {
        return success;
    }

    if (!(pml1[pml1_entry] & PTE_PRESENT)) {
        return success;
    }

    success = true;
    pml1[pml1_entry] = 0;

    __asm__ volatile("invlpg (%0)" :: "r" (vaddr) : "memory");

    return success;
}

static bool pml5_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    bool success = false;

    size_t pml5_entry = (vaddr >> 48) & 0x1ff;
    size_t pml4_entry = (vaddr >> 39) & 0x1ff;
    size_t pml3_entry = (vaddr >> 30) & 0x1ff;
    size_t pml2_entry = (vaddr >> 21) & 0x1ff;
    size_t pml1_entry = (vaddr >> 12) & 0x1ff;

    uint64_t* pml5 = pagemap->top_level;

    uint64_t* pml4 = get_next_level(pml5, pml5_entry, true);
    if (!pml5) {
        return success;
    }

    uint64_t* pml3 = get_next_level(pml4, pml4_entry, true);
    if (!pml3) {
        return success;
    }

    uint64_t* pml2 = get_next_level(pml3, pml3_entry, true);
    if (!pml2) {
        return success;
    }

    uint64_t* pml1 = get_next_level(pml2, pml2_entry, true);
    if (!pml1) {
        return success;
    }

    if (!(pml1[pml1_entry] & PTE_PRESENT)) {
        return success;
    }

    success = true;
    pml1[pml1_entry] = paddr | flags;

    return success;
}

static bool pml5_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    bool success = false;

    size_t pml5_entry = (vaddr >> 48) & 0x1ff;
    size_t pml4_entry = (vaddr >> 39) & 0x1ff;
    size_t pml3_entry = (vaddr >> 30) & 0x1ff;
    size_t pml2_entry = (vaddr >> 21) & 0x1ff;
    size_t pml1_entry = (vaddr >> 12) & 0x1ff;
    uint64_t* pml5 = pagemap->top_level;

    uint64_t* pml4 = get_next_level(pml5, pml5_entry, true);
    if (!pml5) {
        return success;
    }

    uint64_t* pml3 = get_next_level(pml4, pml4_entry, true);
    if (!pml3) {
        return success;
    }

    uint64_t* pml2 = get_next_level(pml3, pml3_entry, true);
    if (!pml2) {
        return success;
    }

    uint64_t* pml1 = get_next_level(pml2, pml2_entry, true);
    if (!pml1) {
        return success;
    }

    if (!(pml1[pml1_entry] & PTE_PRESENT)) {
        return success;
    }

    success = true;
    pml1[pml1_entry] = 0;

    __asm__ volatile("invlpg (%0)" :: "r" (vaddr) : "memory");

    return success;
}

static void pagemap_ctor(struct pagemap* pagemap) {
    uint32_t ecx = 0, unused;

    __get_cpuid(7, &unused, &unused, &ecx, &unused);

    if (ecx & (1 << 16)) {
        pagemap->map_page = pml5_map_page;
        pagemap->unmap_page = pml5_unmap_page;
    } else {
        pagemap->map_page = pml4_map_page;
        pagemap->unmap_page = pml4_unmap_page;
    }

    pagemap->top_level = (uint64_t*) (pmm_alloc(1) + HIGH_VMA);

    if (!kernel_pagemap) {
        for (size_t i = 256; i < 512; i++) {
            get_next_level(pagemap->top_level, i, true);
        }

        uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE);
        uintptr_t text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE);
        uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE);
        uintptr_t rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE);
        uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE);
        uintptr_t data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE);

        uintptr_t kernel_vaddr = kaddr_request.response->virtual_base;
        uintptr_t kernel_paddr = kaddr_request.response->physical_base;

        for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
            uintptr_t paddr = text_addr - kernel_vaddr + kernel_paddr;
            pagemap->map_page(pagemap, text_addr, paddr, PTE_PRESENT);
        }

        for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
            uintptr_t paddr = rodata_addr - kernel_vaddr + kernel_paddr;
            pagemap->map_page(pagemap, rodata_addr, paddr, PTE_PRESENT | PTE_NX);
        }

        for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
            uintptr_t paddr = data_addr - kernel_vaddr + kernel_paddr;
            pagemap->map_page(pagemap, data_addr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
        }

        for (uintptr_t addr = 0x1000; addr < 0x100000000; addr += PAGE_SIZE) {
            pagemap->map_page(pagemap, addr, addr, PTE_PRESENT | PTE_WRITABLE);
            pagemap->map_page(pagemap, addr + HIGH_VMA, addr, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
        }

        struct limine_memmap_response* memmap = memmap_request.response;
        for (size_t i = 0; i < memmap->entry_count; i++) {
            struct limine_memmap_entry* entry = memmap->entries[i];

            uintptr_t base = ALIGN_DOWN(entry->base, PAGE_SIZE);
            uintptr_t top = ALIGN_UP(entry->base + entry->length, PAGE_SIZE);

            if (top <= 0x100000000) {
                continue;
            }

            for (uintptr_t j = base; j < top; j += PAGE_SIZE) {
                if (j < 0x100000000) {
                    continue;
                }

                pagemap->map_page(pagemap, j, j, PTE_PRESENT | PTE_WRITABLE);
                pagemap->map_page(pagemap, j + HIGH_VMA, j, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
            }
        }
    } else {
        for (size_t i = 256; i < 512; i++) {
            pagemap->top_level[i] = kernel_pagemap->top_level[i];
        }
    }
}

void vmm_init(void) {
    pagemap_cache = cache_create("vmm_pagemap", sizeof(struct pagemap), pagemap_ctor, NULL);

    kernel_pagemap = cache_alloc_object(pagemap_cache);
    klog("0x%x\n", (uint32_t) kernel_pagemap);

    //vmm_switch_pagemap(kernel_pagemap);
}

void vmm_switch_pagemap(struct pagemap* pagemap) {
    __asm__ volatile ("mov %0, %%cr3" :: "r" ((void*) pagemap->top_level - HIGH_VMA) : "memory");
}
