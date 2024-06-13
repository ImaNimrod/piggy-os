#include <cpu/asm.h>
#include <cpuid.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/math.h>

extern uint8_t text_start_addr[], text_end_addr[];
extern uint8_t rodata_start_addr[], rodata_end_addr[];
extern uint8_t data_start_addr[], data_end_addr[];

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

#define MASKED_PTE_FLAGS    ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)
#define PAGE_FAULT          14

static struct pagemap kernel_pagemap;

static bool pml4_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    spinlock_acquire(&pagemap->lock);

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml4_index] & PTE_PRESENT)) {
        pagemap->top_level[pml4_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml3 = (uint64_t*) ((pagemap->top_level[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);

    if (flags & PTE_SIZE) {
        pml2[pml2_index] = paddr | flags;
        return true;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        pml2[pml2_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    pml1[pml1_index] = paddr | flags;

    spinlock_release(&pagemap->lock);
    return true;
}

static bool pml4_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml4_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml3 = (uint64_t*) ((pagemap->top_level[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    pml1[pml1_index] = 0;
    invlpg(vaddr);

    spinlock_release(&pagemap->lock);
    return true;
}

static uint64_t* pml4_lowest_level(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml4_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml3 = (uint64_t*) ((pagemap->top_level[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    spinlock_release(&pagemap->lock);
    return pml1 + pml1_index;
}

static bool pml5_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    spinlock_acquire(&pagemap->lock);

    size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;
    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
        pagemap->top_level[pml5_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        pml4[pml4_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml3 = (uint64_t*) ((pagemap->top_level[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);

    if (flags & PTE_SIZE) {
        pml2[pml2_index] = paddr | flags;
        return true;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        pml2[pml2_index] = pmm_allocz(1) | (flags & MASKED_PTE_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    pml1[pml1_index] = paddr | flags;

    spinlock_release(&pagemap->lock);
    return true;
}

static bool pml5_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;
    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        return false;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    pml1[pml1_index] = 0;
    invlpg(vaddr);

    spinlock_release(&pagemap->lock);
    return true;
}

static uint64_t* pml5_lowest_level(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;
    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~(0xfff)) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        return NULL;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~(0xfff)) + HIGH_VMA);
    spinlock_release(&pagemap->lock);
    return pml1 + pml1_index;
}

static void page_fault_handler(struct registers* r) {
    (void) r;
    klog("error code: 0x%x\n", r->error_code);
    klog("rip: 0x%x%x\n", (r->rip >> 32), (uint32_t) r->rip);
    klog("rsp: 0x%x%x\n", (r->rsp >> 32), (uint32_t) r->rsp);
    kpanic(r, "PAGE FAULT");
}

struct pagemap* vmm_get_kernel_pagemap(void) {
    return &kernel_pagemap;
}

struct pagemap* vmm_new_pagemap(void) {
    struct pagemap* pagemap = kmalloc(sizeof(struct pagemap));

    uint32_t ecx = 0, unused;
    __get_cpuid(7, &unused, &unused, &ecx, &unused);
    if (ecx & (1 << 16)) {
        kernel_pagemap.map_page = pml5_map_page;
        kernel_pagemap.unmap_page = pml5_unmap_page;
        kernel_pagemap.lowest_level = pml5_lowest_level;
    } else {
        kernel_pagemap.map_page = pml4_map_page;
        kernel_pagemap.unmap_page = pml4_unmap_page;
        kernel_pagemap.lowest_level = pml4_lowest_level;
    }

    pagemap->top_level = (uint64_t*) (pmm_allocz(1) + HIGH_VMA);
    pagemap->lock = (spinlock_t) {0};

	for (size_t i = 256; i < 512; i++) {
		pagemap->top_level[i] = kernel_pagemap.top_level[i];
    }

    return pagemap;
}

void vmm_switch_pagemap(struct pagemap* pagemap) {
    write_cr3((uintptr_t) pagemap->top_level - HIGH_VMA);
}

void vmm_init(void) {
    klog("[vmm] initializing virtual memory manager...\n");

    uint32_t ecx = 0, unused;
    __get_cpuid(7, &unused, &unused, &ecx, &unused);
    if (ecx & (1 << 16)) {
        kernel_pagemap.map_page = pml5_map_page;
        kernel_pagemap.unmap_page = pml5_unmap_page;
        kernel_pagemap.lowest_level = pml5_lowest_level;
    } else {
        kernel_pagemap.map_page = pml4_map_page;
        kernel_pagemap.unmap_page = pml4_unmap_page;
        kernel_pagemap.lowest_level = pml4_lowest_level;
    }

    kernel_pagemap.top_level = (uint64_t*) (pmm_allocz(1) + HIGH_VMA);
    kernel_pagemap.lock = (spinlock_t) {0};
    
    /*
    for (uint64_t i = 256; i < 512; i++) {
        kernel_pagemap.top_level[i] = pmm_allocz(1) | PTE_PRESENT | PTE_WRITABLE;
    }
    */

    for (uintptr_t i = 0x1000; i < 0x100000000; i += PAGE_SIZE) {
        kernel_pagemap.map_page(&kernel_pagemap, i + HIGH_VMA, i, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
        kernel_pagemap.map_page(&kernel_pagemap, i, i, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
    }

    uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE),
              text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE);
    uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE),
              rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE);
    uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE),
              data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE);

    klog("[vmm] creating kernel virtual memory mappings...\n");

    struct limine_kernel_address_response* kaddr_response = kaddr_request.response;

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
        uintptr_t paddr = text_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        kernel_pagemap.map_page(&kernel_pagemap, text_addr, ALIGN_DOWN(paddr, PAGE_SIZE), PTE_PRESENT);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        uintptr_t paddr = rodata_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        kernel_pagemap.map_page(&kernel_pagemap, rodata_addr, paddr, PTE_PRESENT | PTE_NX);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        uintptr_t paddr = data_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        kernel_pagemap.map_page(&kernel_pagemap, data_addr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
    }

    vmm_switch_pagemap(&kernel_pagemap);
    klog("[vmm] loaded new kernel pagemap\n");

    isr_install_handler(PAGE_FAULT, false, page_fault_handler);

    klog("[vmm] initialized virtual memory manager\n");
}
