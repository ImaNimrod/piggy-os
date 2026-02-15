#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define MASKED_FLAGS ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)

extern struct limine_executable_address_request executable_address_request;
extern struct limine_memmap_request memmap_request;

extern size_t text_start_addr[], text_end_addr[];
extern size_t rodata_start_addr[], rodata_end_addr[];
extern size_t data_start_addr[], data_end_addr[];

struct pagemap* kernel_pagemap;

static bool is_1gb_page_supported;
static bool pat_supported;
static struct slab_cache* pagemap_cache;

static inline uintptr_t entries_to_vaddr(size_t pml4_index, size_t pml3_index, size_t pml2_index, size_t pml1_index) {
    uintptr_t vaddr = 0;
    vaddr |= pml4_index << 39;
    vaddr |= pml3_index << 30;
    vaddr |= pml2_index << 21;
    vaddr |= pml1_index << 12;
    return vaddr;
}

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
            if (depth == 2) {
                pmm_free((uintptr_t) level - HIGH_VMA, PAGE_SIZE_2MB / PAGE_SIZE_4KB);
            }
        }

        destroy_levels_recursive((uint64_t*) ((level[i] & ~PTE_FLAG_MASK) + HIGH_VMA), 0, 512, depth - 1);
    }

    pmm_free((uintptr_t) level - HIGH_VMA, 1);
}

static void page_fault_handler(struct registers* r, void* arg) {
    (void) arg;

    struct thread* current_thread = this_cpu()->running_thread;

    if (!vmm_page_fault_handler(read_cr2(), r->error_code)) {
        if (current_thread != NULL && current_thread->usercopy_registers != NULL) {
            memcpy64((uint64_t*) r, (const uint64_t*) current_thread->usercopy_registers, sizeof(struct registers) >> 3);
            current_thread->usercopy_registers = NULL;
            r->rax = -EFAULT;
        } else {
            kpanic(r, true, "fatal pagefault in pid: %d, tid: %d",
                    current_thread->process->pid, current_thread->tid);
        }
    }
}

struct pagemap* pagemap_create(void) {
    struct pagemap* new_pagemap = slab_cache_alloc(pagemap_cache);
    if (unlikely(new_pagemap == NULL)) {
        return NULL;
    }

    new_pagemap->top_level = (uint64_t*) (pmm_alloc_zero(1) + HIGH_VMA);
    for (size_t i = 256; i < 512; i++) {
        new_pagemap->top_level[i] = kernel_pagemap->top_level[i];
    }

    spinlock_init(&new_pagemap->lock);

    return new_pagemap;
}

bool pagemap_destroy(struct pagemap* pagemap) {
    spinlock_acquire(&pagemap->lock);
    destroy_levels_recursive(pagemap->top_level, 0, 256, 4);
    return slab_cache_free(pagemap_cache, pagemap);
}

void pagemap_invalidate(uintptr_t vaddr, size_t size) {
    size_t page_count = DIV_CEIL(size, PAGE_SIZE_4KB);
    if (page_count <= 16) {
        vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE_4KB);
        for (size_t i = 0; i < size; i += PAGE_SIZE_4KB) {
            invlpg(vaddr + i);
        }
    } else {
        write_cr3(read_cr3());
    }
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
        if (is_1gb_page_supported) {
            pml3[pml3_index] = paddr | flags | PTE_SIZE;
        } else {
            for (size_t i = 0; i < PAGE_SIZE_1GB; i += PAGE_SIZE_2MB) {
                pagemap_map(pagemap, vaddr + i, paddr + i, flags, PAGE_SIZE_2MB);
            }
        }

        spinlock_release(&pagemap->lock);
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

bool pagemap_unmap(struct pagemap* pagemap, uintptr_t vaddr, page_size_t* out_size) {
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
        *out_size = PAGE_SIZE_1GB;
        ret = true;
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    if (pml2[pml2_index] & PTE_SIZE) {
        pml2[pml2_index] = 0;
        *out_size = PAGE_SIZE_2MB;
        ret = true;
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = 0;
    *out_size = PAGE_SIZE_4KB;
    ret = true;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

bool pagemap_remap(struct pagemap* pagemap, uintptr_t vaddr, uint64_t flags) {
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
        pml3[pml3_index] = (pml3[pml3_index] & ~PTE_FLAG_MASK) | (flags & PTE_FLAG_MASK);
        ret = true;
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    if (pml2[pml2_index] & PTE_SIZE) {
        pml2[pml2_index] = (pml2[pml2_index] & ~PTE_FLAG_MASK) | (flags & PTE_FLAG_MASK);
        ret = true;
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = (pml1[pml1_index] & ~PTE_FLAG_MASK) | (flags & PTE_FLAG_MASK);
    ret = true;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

void pagemap_map_range(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, size_t length, uint64_t flags) {
    if (vaddr % PAGE_SIZE_4KB || paddr % PAGE_SIZE_4KB || length % PAGE_SIZE_4KB) {
        kpanic(NULL, true, "unaligned arguments to pagemap_map_range");
    }

    size_t page_count;
    page_size_t page_size;

    if ((vaddr % PAGE_SIZE_2MB == 0) && (paddr % PAGE_SIZE_2MB == 0) && (length % PAGE_SIZE_2MB == 0)) {
        page_count = length / PAGE_SIZE_2MB;
        page_size = PAGE_SIZE_2MB;
    } else {
        page_count = length / PAGE_SIZE_4KB;
        page_size = PAGE_SIZE_4KB;
    }

    for (size_t i = 0; i < page_count; i++) {
        pagemap_map(pagemap, vaddr, paddr, flags, page_size);
        vaddr += page_size;
        paddr += page_size;
    }
}

bool pagemap_unmap_range(struct pagemap* pagemap, uintptr_t vaddr, size_t length) {
    if (vaddr % PAGE_SIZE_4KB || length % PAGE_SIZE_4KB) {
        kpanic(NULL, true, "unaligned arguments to pagemap_unmap_range");
    }

    size_t page_count;
    page_size_t page_size;

    if ((vaddr % PAGE_SIZE_2MB == 0) && (length % PAGE_SIZE_2MB == 0)) {
        page_count = length / PAGE_SIZE_2MB;
        page_size = PAGE_SIZE_2MB;
    } else {
        page_count = length / PAGE_SIZE_4KB;
        page_size = PAGE_SIZE_4KB;
    }

    for (size_t i = 0; i < page_count; i++) {
        pagemap_unmap(pagemap, vaddr, &page_size);
        vaddr += page_size;
    }

    return true;
}

uint64_t pagemap_get_mapping(struct pagemap* pagemap, uintptr_t vaddr, page_size_t* out_size) {
    spinlock_acquire(&pagemap->lock);

    uint64_t ret = 0;

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
        ret = pml3[pml3_index];
        *out_size = PAGE_SIZE_1GB;
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    if (pml2[pml2_index] & PTE_SIZE) {
        ret = pml2[pml2_index];
        *out_size = PAGE_SIZE_2MB;
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    ret = pml1[pml1_index];
    *out_size = PAGE_SIZE_4KB;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

void paging_init(void) {
    uint32_t edx, unused;
    if (cpuid(0x80000001, 0, &unused, &unused, &unused, &edx) && (edx & (1 << 26))) {
        is_1gb_page_supported = true;
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
    for (size_t i = 256; i < 512; i++) {
        kernel_pagemap->top_level[i] = pmm_alloc_zero(1) | PTE_PRESENT | PTE_WRITABLE;
    }

    spinlock_init(&kernel_pagemap->lock);

    uintptr_t paddr = 0;

    struct limine_memmap_response* memmap_response = memmap_request.response;

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* memmap_entry = memmap_response->entries[i];

        uint64_t flags = PTE_PRESENT | PTE_NX;
        switch (memmap_entry->type) {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
                flags |= PTE_WRITABLE;
                break;
            case LIMINE_MEMMAP_FRAMEBUFFER:
                flags |= PTE_WRITABLE | (pat_supported ? PTE_WRITE_COMBINE : 0);
                break;
            default:
                continue;
        }

        paddr = ALIGN_DOWN(memmap_entry->base, PAGE_SIZE_4KB);
        pagemap_map_range(kernel_pagemap, paddr + HIGH_VMA, paddr, ALIGN_UP(memmap_entry->length, PAGE_SIZE_4KB), flags);
    }

    struct limine_executable_address_response* kernel_address_response = executable_address_request.response;

    uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE_4KB);
    uintptr_t text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE_4KB);

    uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE_4KB);
    uintptr_t rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE_4KB);

    uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE_4KB);
    uintptr_t data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE_4KB);

    pagemap_map_range(kernel_pagemap,
            text_start, text_start - kernel_address_response->virtual_base + kernel_address_response->physical_base,
            text_end - text_start,
            PTE_PRESENT | PTE_GLOBAL);

    pagemap_map_range(kernel_pagemap,
            rodata_start, rodata_start - kernel_address_response->virtual_base + kernel_address_response->physical_base,
            rodata_end - rodata_start,
            PTE_PRESENT | PTE_GLOBAL | PTE_NX);

    pagemap_map_range(kernel_pagemap,
            data_start, data_start - kernel_address_response->virtual_base + kernel_address_response->physical_base,
            data_end - data_start,
            PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX);

    pagemap_load(kernel_pagemap);

    isr_register_handler(14, page_fault_handler, NULL);

    klog("[paging] initialized kernel pagemap\n");
}
