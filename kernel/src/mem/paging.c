#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

#define MASKED_FLAGS ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)

extern struct limine_executable_address_request executable_address_request;
extern struct limine_memmap_request memmap_request;

extern size_t limine_requests_start_addr[], limine_requests_end_addr[];
extern size_t text_start_addr[], text_end_addr[];
extern size_t rodata_start_addr[], rodata_end_addr[];
extern size_t data_start_addr[], data_end_addr[];

struct pagemap* kernel_pagemap = NULL;

static bool hugepages_supported = false;
static bool pat_supported = false;
static struct slab_cache* pagemap_cache = NULL;

static void destroy_levels_recursive(uint64_t* level, size_t start, size_t end, size_t depth) {
    if (depth == 1) {
        pmm_free((uintptr_t) level - HIGH_VMA, 1);
        return;
    }

    for (size_t i = start; i < end; i++) {
        if (!(level[i] & PTE_PRESENT)) {
            continue;
        }

        if (level[i] & PTE_SIZE) {
            if (depth == 3) {
                pmm_free((uintptr_t) level - HIGH_VMA, PAGE_SIZE_1GB / PAGE_SIZE_4KB);
            } else if (depth == 2) {
                pmm_free((uintptr_t) level - HIGH_VMA, PAGE_SIZE_2MB / PAGE_SIZE_4KB);
            }
        }

        destroy_levels_recursive((uint64_t*) ((level[i] & ~PTE_FLAG_MASK) + HIGH_VMA), 0, 512, depth - 1);
    }

    pmm_free((uintptr_t) level - HIGH_VMA, 1);
}

static inline uintptr_t entries_to_vaddr(size_t pml4_index, size_t pml3_index, size_t pml2_index, size_t pml1_index) {
    uintptr_t vaddr = 0;
    vaddr |= pml4_index << 39;
    vaddr |= pml3_index << 30;
    vaddr |= pml2_index << 21;
    vaddr |= pml1_index << 12;
    return vaddr;
}

static void page_fault_handler(struct registers* r, void* arg) {
    (void) arg;
    kpanic(r, true, "fatal pagefault");
}

struct pagemap* pagemap_create(void) {
    struct pagemap* new_pagemap = slab_cache_alloc(pagemap_cache);
    if (unlikely(new_pagemap == NULL)) {
        return NULL;
    }

    new_pagemap->top_level = (uint64_t*) (pmm_alloc_zero(1) + HIGH_VMA);
    new_pagemap->lock = (spinlock_t) {0};

    for (size_t i = 256; i < 512; i++) {
        new_pagemap->top_level[i] = kernel_pagemap->top_level[i];
    }

    return new_pagemap;
}

bool pagemap_destroy(struct pagemap* pagemap) {
    spinlock_acquire(&pagemap->lock);
    destroy_levels_recursive(pagemap->top_level, 0, 256, 4);
    return slab_cache_free(pagemap_cache, pagemap);
}

// TODO: implement COW and other speedups because this is painfully slow
struct pagemap* pagemap_fork(struct pagemap* old_pagemap) {
    struct pagemap* new_pagemap = pagemap_create();
    if (unlikely(new_pagemap == NULL)) {
        return NULL;
    }

    for (size_t i = 0; i < 256; i++) {
        if (old_pagemap->top_level[i] & PTE_PRESENT) {
            uint64_t* pml4 = (uint64_t*) ((old_pagemap->top_level[i] & ~PTE_FLAG_MASK) + HIGH_VMA);

            for (size_t j = 0; j < 512; j++) {
                if (pml4[j] & PTE_PRESENT) {
                    uint64_t* pml3 = (uint64_t*) ((pml4[j] & ~PTE_FLAG_MASK) + HIGH_VMA);

                    for (size_t k = 0; k < 512; k++) {
                        if (pml3[k] & PTE_PRESENT) {
                            uint64_t* pml2 = (uint64_t*) ((pml3[k] & ~PTE_FLAG_MASK) + HIGH_VMA);

                            for (size_t l = 0; l < 512; l++) {
                                if (pml2[l] & PTE_PRESENT) {
                                    uintptr_t paddr = pmm_alloc_zero(1);
                                    memcpy64((void*) (paddr + HIGH_VMA), (void*) ((pml2[l] & ~PTE_FLAG_MASK) + HIGH_VMA), PAGE_SIZE_4KB >> 3);
                                    pagemap_map(new_pagemap, entries_to_vaddr(i, j, k, l), paddr, pml2[l] & PTE_FLAG_MASK, PAGE_SIZE_4KB);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return new_pagemap;
}

void pagemap_load(struct pagemap* pagemap) {
    write_cr3((uint64_t) pagemap->top_level - HIGH_VMA);
}

void pagemap_map(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags, page_size_t size) {
    spinlock_acquire(&pagemap->lock);

    size_t pml4_index = (vaddr >> 39) & 0x1ff;
    size_t pml3_index = (vaddr >> 30) & 0x1ff;
    size_t pml2_index = (vaddr >> 21) & 0x1ff;
    size_t pml1_index = (vaddr >> 12) & 0x1ff;

    uint64_t* pml4 = pagemap->top_level;
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        pml4[pml4_index] = pmm_alloc_zero(1) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (size == PAGE_SIZE_1GB) {
        spinlock_release(&pagemap->lock);

        if (hugepages_supported) {
            pml3[pml3_index] = paddr | flags | PTE_SIZE;
        } else {
            for (uint64_t i = 0; i < PAGE_SIZE_1GB; i += PAGE_SIZE_2MB) {
                pagemap_map(pagemap, vaddr + i, paddr + i, flags, PAGE_SIZE_2MB);
            }
        }

        return;
    }

    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_alloc_zero(1) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (size == PAGE_SIZE_2MB) {
        pml2[pml2_index] = paddr | flags | PTE_SIZE;
        spinlock_release(&pagemap->lock);
        return;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        pml2[pml2_index] = pmm_alloc_zero(1) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (flags & ((uint64_t) (1 << 12))) {
        flags &= ~((uint64_t) (1 << 12));
        flags |= ((uint64_t) (1 << 7));
    }

    pml1[pml1_index] = paddr | flags;
    spinlock_release(&pagemap->lock);
}

bool pagemap_unmap(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    bool ret = false;

    size_t pml4_index = (vaddr >> 39) & 0x1ff;
    size_t pml3_index = (vaddr >> 30) & 0x1ff;
    size_t pml2_index = (vaddr >> 21) & 0x1ff;
    size_t pml1_index = (vaddr >> 12) & 0x1ff;

    uint64_t* pml4 = pagemap->top_level;
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        goto end;
    }

    if (pml3[pml3_index] & PTE_SIZE) {
        pml3[pml3_index] = 0;
        invlpg(vaddr);
        ret = true;
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    if (pml2[pml2_index] & PTE_SIZE) {
        pml2[pml2_index] = 0;
        invlpg(vaddr);
        ret = true;
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = 0;
    invlpg(vaddr);

    ret = true;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

void paging_init(void) {
    uint32_t edx, unused;
    if (cpuid(0x80000001, 0, &unused, &unused, &unused, &edx) && (edx & (1 << 26))) {
        hugepages_supported = true;
    }
    if (cpuid(1, 0, &unused, &unused, &unused, &edx) && (edx & (1 << 16))) {
        pat_supported = true;
    }

    pagemap_cache = slab_cache_create("struct pagemap cache", sizeof(struct pagemap));
    if (unlikely(pagemap_cache == NULL)) {
        kpanic(NULL, false, "failed to create object cache for pagemap structs");
    }

    kernel_pagemap = slab_cache_alloc(pagemap_cache);
    if (unlikely(kernel_pagemap == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for kernel pagemap");
    }

    kernel_pagemap->top_level = (uint64_t*) (pmm_alloc_zero(1) + HIGH_VMA);
    kernel_pagemap->lock = (spinlock_t) {0};

    for (size_t i = 256; i < 512; i++) {
        kernel_pagemap->top_level[i] = pmm_alloc_zero(1) | PTE_PRESENT | PTE_WRITABLE;
    }

    uintptr_t paddr = 0;

    struct limine_memmap_response* memmap_response = memmap_request.response;

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* memmap_entry = memmap_response->entries[i];
        if (memmap_entry->type == LIMINE_MEMMAP_RESERVED || memmap_entry->type == LIMINE_MEMMAP_BAD_MEMORY) {
            continue;
        }

        uint64_t flags = PTE_PRESENT | PTE_NX;
        switch (memmap_entry->type) {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
                flags |= PTE_WRITABLE;
                break;
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            case LIMINE_MEMMAP_ACPI_NVS:
                flags |= PTE_CACHE_DISABLE;
                break;
            case LIMINE_MEMMAP_FRAMEBUFFER:
                flags |= PTE_WRITABLE;
                if (pat_supported) {
                    flags |= PTE_WRITE_COMBINE;
                }
                break;
        }

        paddr = ALIGN_DOWN(memmap_entry->base, PAGE_SIZE_4KB);

        for (size_t j = 0; j < DIV_CEIL(memmap_entry->length, PAGE_SIZE_4KB); j++) {
            pagemap_map(kernel_pagemap, paddr + HIGH_VMA, paddr, flags, PAGE_SIZE_4KB);
            paddr += PAGE_SIZE_4KB;
        }
    }

    struct limine_executable_address_response* kernel_address_response = executable_address_request.response;

    uintptr_t limine_requests_start = ALIGN_DOWN((uintptr_t) limine_requests_start_addr, PAGE_SIZE_4KB);
    uintptr_t limine_requests_end = ALIGN_UP((uintptr_t) limine_requests_end_addr, PAGE_SIZE_4KB);

    uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE_4KB);
    uintptr_t text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE_4KB);

    uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE_4KB);
    uintptr_t rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE_4KB);

    uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE_4KB);
    uintptr_t data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE_4KB);

    for (uintptr_t limine_requests_addr = limine_requests_start; limine_requests_addr < limine_requests_end; limine_requests_addr += PAGE_SIZE_4KB) {
        paddr = limine_requests_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        pagemap_map(kernel_pagemap, limine_requests_addr, ALIGN_DOWN(paddr, PAGE_SIZE_4KB), PTE_PRESENT | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);
    }

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE_4KB) {
        paddr = text_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        pagemap_map(kernel_pagemap, text_addr, ALIGN_DOWN(paddr, PAGE_SIZE_4KB), PTE_PRESENT | PTE_GLOBAL, PAGE_SIZE_4KB);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE_4KB) {
        paddr = rodata_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        pagemap_map(kernel_pagemap, rodata_addr, ALIGN_DOWN(paddr, PAGE_SIZE_4KB), PTE_PRESENT | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE_4KB) {
        paddr = data_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        pagemap_map(kernel_pagemap, data_addr, ALIGN_DOWN(paddr, PAGE_SIZE_4KB), PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);
    }

    pagemap_load(kernel_pagemap);

    isr_register_handler(14, page_fault_handler, NULL);

    klog("[paging] initialized kernel pagemap\n");
}
