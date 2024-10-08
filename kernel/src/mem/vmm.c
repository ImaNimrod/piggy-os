#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/sched.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

#define MASKED_FLAGS ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)
#define PAGE_FAULT_VECTOR 14

extern uint8_t text_start_addr[], text_end_addr[];
extern uint8_t unmap_after_init_start_addr[], unmap_after_init_end_addr[];
extern uint8_t rodata_start_addr[], rodata_end_addr[];
extern uint8_t data_start_addr[], data_end_addr[];
extern uint8_t ro_after_init_start_addr[], ro_after_init_end_addr[];

READONLY_AFTER_INIT struct pagemap* kernel_pagemap = NULL;

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static struct cache* pagemap_cache = NULL;

static void destroy_levels_recursive(uint64_t* level, size_t start, size_t end, size_t depth) {
    if (depth < 1) {
        return;
    } else if (depth == 1) {
        pmm_free((uintptr_t) level - HIGH_VMA, 1);
        return;
    }

    for (size_t i = start; i < end; i++) {
        if (!(level[i] & PTE_PRESENT)) {
            continue;
        }

        if ((level[i] & PTE_SIZE) && depth == 2) {
            pmm_free((uintptr_t) level - HIGH_VMA, BIGPAGE_SIZE / PAGE_SIZE);
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

static void page_fault_handler(struct registers* r, void* ctx) {
    (void) ctx;

    uintptr_t faulting_addr = read_cr2();
    bool is_present = r->error_code & FAULT_PRESENT;
    bool is_writing = r->error_code & FAULT_WRITABLE;
    bool is_user = r->error_code & FAULT_USER;

    klog("[vmm] page fault occurred when %s process tried to %s %spresent page entry for address 0x%p\n",
            is_user ? "user-mode" : "supervisor-mode",
            is_writing ? "write to" : "read from",
            is_present ? "\0" : "non-",
            faulting_addr);

    struct thread* current_thread = this_cpu()->running_thread;
    if (current_thread != NULL) {
        struct process* current_process = current_thread->process;

        if (current_process->pid != 0) {
            klog("[vmm] killing thread (pid: %d, tid: %d) due to page fault\n", current_process->pid, current_thread->tid);
            sched_thread_dequeue(current_thread);
            sched_yield();
            return;
        }
    } 

    kpanic(r, true, "page fault occurred in kernel");
}

struct pagemap* vmm_new_pagemap(void) {
    struct pagemap* pagemap = cache_alloc_object(pagemap_cache);
    if (unlikely(pagemap == NULL)) {
        return NULL;
    }

    pagemap->top_level = (uint64_t*) pmm_allocz(1);
    if (pagemap->top_level == NULL) {
        cache_free_object(pagemap_cache, pagemap);
        return NULL;
    }

    pagemap->top_level = (uint64_t*) ((uintptr_t) pagemap->top_level + HIGH_VMA);
    pagemap->lock = (spinlock_t) {0};

    uint32_t ecx = 0, unused;
    if (cpuid(7, 0, &unused, &unused, &ecx, &unused) && ecx & (1 << 16)) {
        pagemap->has_level5 = true;
    }

    for (size_t i = 256; i < 512; i++) {
        pagemap->top_level[i] = kernel_pagemap->top_level[i];
    }

    return pagemap;
}

void vmm_destroy_pagemap(struct pagemap* pagemap) {
    spinlock_acquire(&pagemap->lock);

    destroy_levels_recursive(pagemap->top_level, 0, 256, (pagemap->has_level5 ? 5 : 4));

    pmm_free((uintptr_t) pagemap->top_level - HIGH_VMA, 1);
    cache_free_object(pagemap_cache, pagemap);
}

struct pagemap* vmm_fork_pagemap(struct pagemap* old_pagemap) {
    struct pagemap* new_pagemap = vmm_new_pagemap();
    if (new_pagemap == NULL) {
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
                                    uintptr_t paddr = pmm_allocz(1);
                                    memcpy((void*) (paddr + HIGH_VMA),
                                            (void*) ((pml2[l] & ~PTE_FLAG_MASK) + HIGH_VMA), PAGE_SIZE);
                                    vmm_map_page(new_pagemap, entries_to_vaddr(i, j, k, l), paddr,
                                            pml2[l] & PTE_FLAG_MASK);
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

void vmm_switch_pagemap(struct pagemap* pagemap) {
    write_cr3((uintptr_t) pagemap->top_level - HIGH_VMA);
}

bool vmm_map_page(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags) {
    spinlock_acquire(&pagemap->lock);

    bool ret = false;

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4;

    if (pagemap->has_level5) {
        size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;

        if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
            pagemap->top_level[pml5_index] = pmm_allocz(1);
            if (pagemap->top_level[pml5_index] == 0) {
                goto end;
            }

            pagemap->top_level[pml5_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
        }

        pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    } else {
        pml4 = pagemap->top_level;
    }

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        pml4[pml4_index] = pmm_allocz(1);
        if (pml4[pml4_index] == 0) {
            goto end;
        }

        pml4[pml4_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_allocz(1);
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
        pml2[pml2_index] = pmm_allocz(1);
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

    uint64_t* pml4;

    if (pagemap->has_level5) {
        size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;

        if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
            goto end;
        }

        pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    } else {
        pml4 = pagemap->top_level;
    }

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

bool vmm_update_flags(struct pagemap* pagemap, uintptr_t vaddr, uint64_t flags) {
    spinlock_acquire(&pagemap->lock);

    bool ret = false;

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4;

    if (pagemap->has_level5) {
        size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;

        if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
            goto end;
        }

        pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    } else {
        pml4 = pagemap->top_level;
    }

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (pml2[pml2_index] & PTE_SIZE) {
        uintptr_t paddr = pml2[pml2_index] & ~PTE_FLAG_MASK;
        pml2[pml2_index] = paddr | flags;
        invlpg(vaddr);
        ret = true;
        goto end;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    uintptr_t paddr = pml1[pml1_index] & ~PTE_FLAG_MASK;
    pml1[pml1_index] = paddr | flags;
    invlpg(vaddr);

    ret = true;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}


uintptr_t vmm_get_page_mapping(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    uintptr_t ret = (uintptr_t) -1;

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4;

    if (pagemap->has_level5) {
        size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;

        if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
            goto end;
        }

        pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    } else {
        pml4 = pagemap->top_level;
    }

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (pml2[pml2_index] & PTE_SIZE) {
        ret = pml2[pml2_index];
        goto end;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        goto end;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    ret = (pml1[pml1_index] & PTE_PRESENT) ? pml1[pml1_index] : (uintptr_t) -1;

end:
    spinlock_release(&pagemap->lock);
    return ret;
}

UNMAP_AFTER_INIT void vmm_readonly_data_after_init(void) {
    uintptr_t ro_after_init_start = ALIGN_DOWN((uintptr_t) ro_after_init_start_addr, PAGE_SIZE),
              ro_after_init_end = ALIGN_UP((uintptr_t) ro_after_init_end_addr, PAGE_SIZE);

    for (uintptr_t ro_after_init_addr = ro_after_init_start; ro_after_init_addr < ro_after_init_end; ro_after_init_addr += PAGE_SIZE) {
        if (!vmm_update_flags(kernel_pagemap, ro_after_init_addr, PTE_PRESENT | PTE_NX)) {
            kpanic(NULL, false, "vmm_readonly_data_after_init: failed to update page mapping");
        }
    }
}

void vmm_unmap_text_after_init(void) {
    uintptr_t unmap_after_init_start = ALIGN_DOWN((uintptr_t) unmap_after_init_start_addr, PAGE_SIZE),
              unmap_after_init_end = ALIGN_UP((uintptr_t) unmap_after_init_end_addr, PAGE_SIZE);

    for (uintptr_t unmap_after_init_addr = unmap_after_init_start; unmap_after_init_addr < unmap_after_init_end; unmap_after_init_addr += PAGE_SIZE) {
        if (!vmm_unmap_page(kernel_pagemap, unmap_after_init_addr)) {
            kpanic(NULL, false, "vmm_unmap_text_after_init: failed to unmap page");
        }
    }

    struct limine_kernel_address_response* kaddr_response = kaddr_request.response;
    uintptr_t start_paddr = unmap_after_init_start - kaddr_response->virtual_base + kaddr_response->physical_base;
    pmm_free(start_paddr, DIV_CEIL(unmap_after_init_end, unmap_after_init_start));
}

UNMAP_AFTER_INIT void vmm_init(void) {
    klog("[vmm] initializing virtual memory manager...\n");

    pagemap_cache = slab_cache_create("pagemap cache", sizeof(struct pagemap));
    if (pagemap_cache == NULL) {
        kpanic(NULL, false, "failed to initialize object cache for pagemaps");
    }

    kernel_pagemap = cache_alloc_object(pagemap_cache);
    kernel_pagemap->top_level = (uint64_t*) pmm_allocz(1);
    if (kernel_pagemap->top_level == NULL) {
        kpanic(NULL, false, "failed to allocate memory for kernel pagemap top level");
    }

    kernel_pagemap->top_level = (uint64_t*) ((uintptr_t) kernel_pagemap->top_level + HIGH_VMA);
    kernel_pagemap->lock = (spinlock_t) {0};

    uint32_t ecx = 0, unused;
    if (cpuid(7, 0, &unused, &unused, &ecx, &unused) && ecx & (1 << 16)) {
        kernel_pagemap->has_level5 = true;
    }

    for (uint64_t i = 256; i < 512; i++) {
        kernel_pagemap->top_level[i] = pmm_allocz(1) | PTE_PRESENT | PTE_WRITABLE;
    }

    klog("[vmm] creating kernel virtual memory mappings...\n");

    uintptr_t paddr = 0;
    for (size_t i = 1; i < 0x800; i++) {
        vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
        paddr += BIGPAGE_SIZE;
    }

    struct limine_memmap_response* memmap_response = memmap_request.response;
    uint64_t entry_count = memmap_response->entry_count;

    for (size_t i = 0; i < entry_count; i++) {
        struct limine_memmap_entry* memmap_entry = memmap_response->entries[i];
        if (memmap_entry->type != LIMINE_MEMMAP_USABLE && memmap_entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE
                && memmap_entry->type != LIMINE_MEMMAP_KERNEL_AND_MODULES && memmap_entry->type != LIMINE_MEMMAP_FRAMEBUFFER) {
            continue;
        }

        paddr = ALIGN_DOWN(memmap_entry->base, BIGPAGE_SIZE);

        for (size_t j = 0; j < DIV_CEIL(memmap_entry->length, BIGPAGE_SIZE); j++) {
            vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                    PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
            paddr += BIGPAGE_SIZE;
        }
    }

    struct limine_kernel_address_response* kaddr_response = kaddr_request.response;

    uintptr_t text_start = ALIGN_DOWN((uintptr_t) text_start_addr, PAGE_SIZE),
              text_end = ALIGN_UP((uintptr_t) text_end_addr, PAGE_SIZE);
    uintptr_t rodata_start = ALIGN_DOWN((uintptr_t) rodata_start_addr, PAGE_SIZE),
              rodata_end = ALIGN_UP((uintptr_t) rodata_end_addr, PAGE_SIZE);
    uintptr_t data_start = ALIGN_DOWN((uintptr_t) data_start_addr, PAGE_SIZE),
              data_end = ALIGN_UP((uintptr_t) data_end_addr, PAGE_SIZE);

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
        paddr = text_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        vmm_map_page(kernel_pagemap, text_addr, ALIGN_DOWN(paddr, PAGE_SIZE), PTE_PRESENT | PTE_GLOBAL);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        paddr = rodata_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        vmm_map_page(kernel_pagemap, rodata_addr, paddr, PTE_PRESENT | PTE_GLOBAL | PTE_NX);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        paddr = data_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        vmm_map_page(kernel_pagemap, data_addr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX);
    }

    vmm_switch_pagemap(kernel_pagemap);
    klog("[vmm] switched to new kernel pagemap\n");

    isr_install_handler(PAGE_FAULT_VECTOR, page_fault_handler, NULL);

    klog("[vmm] initialized virtual memory manager\n");
}
