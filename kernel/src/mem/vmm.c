#include <cpu/asm.h>
#include <cpu/isr.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

#include <cpu/smp.h>

#define MASKED_FLAGS ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)

extern struct limine_executable_address_request executable_address_request;
extern struct limine_memmap_request memmap_request;

extern size_t limine_requests_start_addr[], limine_requests_end_addr[];
extern size_t text_start_addr[], text_end_addr[];
extern size_t rodata_start_addr[], rodata_end_addr[];
extern size_t data_start_addr[], data_end_addr[];

struct pagemap {
    uint64_t* top_level;
    spinlock_t lock;
};

struct pagemap* kernel_pagemap = NULL;

static struct slab_cache* pagemap_cache = NULL;

// TODO: page fault handling
static void page_fault_handler(struct registers* r, void* arg) {
    (void) arg;

    kpanic(r, false, "pagefault!\n");
}

bool vmm_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    spinlock_acquire(&pagemap->lock);

    bool ret = false;

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4 = pagemap->top_level;
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        pml4[pml4_index] = pmm_alloc_zero(1);
        if (pml4[pml4_index] == 0) {
            goto end;
        }

        pml4[pml4_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_alloc_zero(1);
        if (pml3[pml3_index] == 0) {
            goto end;
        }

        pml3[pml3_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (flags & PTE_SIZE) {
        pml2[pml2_index] = paddr | flags;
        ret = true;
        goto end;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        pml2[pml2_index] = pmm_alloc_zero(1);
        if (pml2[pml2_index] == 0) {
            goto end;
        }

        pml2[pml2_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = paddr | flags;

    ret = true;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

bool vmm_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    bool ret = false;

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4 = pagemap->top_level;
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (pml2[pml2_index] & PTE_SIZE) {
        pml2[pml2_index] = 0;
        invlpg(vaddr);
        ret = true;
        goto end;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
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

struct pagemap* vmm_create_pagemap(void) {
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

bool vmm_destroy_pagemap(struct pagemap* pagemap) {
    pmm_free((uintptr_t) pagemap->top_level - HIGH_VMA, 1);
    return slab_cache_free(pagemap_cache, pagemap);
}

void vmm_switch_pagemap(struct pagemap* pagemap) {
    write_cr3((uint64_t) pagemap->top_level - HIGH_VMA);
}

void vmm_init(void) {
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

    for (size_t i = 1; i < 0x800; i++) {
        vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                     PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
        paddr += BIGPAGE_SIZE;
    }

    struct limine_memmap_response* memmap_response = memmap_request.response;

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* memmap_entry = memmap_response->entries[i];
        if (memmap_entry->type != LIMINE_MEMMAP_USABLE && memmap_entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES && memmap_entry->type != LIMINE_MEMMAP_FRAMEBUFFER) {
            continue;
        }

        paddr = ALIGN_DOWN(memmap_entry->base, BIGPAGE_SIZE);

        for (size_t j = 0; j < DIV_CEIL(memmap_entry->length, BIGPAGE_SIZE); j++) {
            vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                         PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
            paddr += BIGPAGE_SIZE;
        }
    }

    struct limine_executable_address_response* kernel_address_response = executable_address_request.response;

    uintptr_t limine_requests_start = ALIGN_DOWN((uintptr_t) limine_requests_start_addr, PAGE_SIZE);
    uintptr_t limine_requests_end = ALIGN_UP((uintptr_t) limine_requests_end_addr, PAGE_SIZE);

    uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE);
    uintptr_t text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE);

    uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE);
    uintptr_t rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE);

    uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE);
    uintptr_t data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE);

    for (uintptr_t limine_requests_addr = limine_requests_start; limine_requests_addr < limine_requests_end; limine_requests_addr += PAGE_SIZE) {
        paddr = limine_requests_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        vmm_map_page(kernel_pagemap, limine_requests_addr, ALIGN_DOWN(paddr, PAGE_SIZE), PTE_PRESENT | PTE_GLOBAL | PTE_NX);
    }

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
        paddr = text_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        vmm_map_page(kernel_pagemap, text_addr, ALIGN_DOWN(paddr, PAGE_SIZE), PTE_PRESENT | PTE_GLOBAL);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        paddr = rodata_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        vmm_map_page(kernel_pagemap, rodata_addr, paddr, PTE_PRESENT | PTE_GLOBAL | PTE_NX);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        paddr = data_addr - kernel_address_response->virtual_base + kernel_address_response->physical_base;
        vmm_map_page(kernel_pagemap, data_addr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX);
    }

    vmm_switch_pagemap(kernel_pagemap);

    isr_register_handler(14, page_fault_handler, NULL);

    klog("[vmm] initialized virtual memory manager\n");
}
