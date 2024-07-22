#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

#define MASKED_FLAGS ~(PTE_SIZE | PTE_GLOBAL | PTE_NX)
#define PAGE_FAULT 14

extern uint8_t text_start_addr[], text_end_addr[];
extern uint8_t rodata_start_addr[], rodata_end_addr[];
extern uint8_t data_start_addr[], data_end_addr[];

struct pagemap* kernel_pagemap;

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static struct cache* pagemap_cache;

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

static void page_fault_handler(struct registers* r) {
    uintptr_t faulting_addr = read_cr2();
    bool is_present = r->error_code & FAULT_PRESENT;
    bool is_writing = r->error_code & FAULT_WRITABLE;
    bool is_user = r->error_code & FAULT_USER;

    klog("[vmm] page fault occurred when %s process tried to %s %spresent page entry for address 0x%x\n",
            is_user ? "user-mode" : "supervisor-mode",
            is_writing ? "write to" : "read from",
            is_present ? "\0" : "non-",
            faulting_addr);

    struct thread* current_thread = this_cpu()->running_thread;
    if (current_thread != NULL) {
        struct process* current_process = current_thread->process;
        if (current_process->pid != 0) {
            klog("[vmm] terminating process (pid = %d) due to page fault\n", current_process->pid);
            process_destroy(current_process, -1);
            return;
        }
    }

    kpanic(r, "page fault occurred in kernel");
}

struct pagemap* vmm_new_pagemap(void) {
    struct pagemap* pagemap = kmalloc(sizeof(struct pagemap));
    if (pagemap == NULL) {
        return NULL;
    }

    pagemap->top_level = (uint64_t*) pmm_allocz(1);
    if (pagemap->top_level == NULL) {
        kfree(pagemap);
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
    kfree(pagemap);
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
                                    uintptr_t paddr = pmm_alloc(1);
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
                spinlock_release(&pagemap->lock);
                return false;
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
            spinlock_release(&pagemap->lock);
            return false;
        }

        pml4[pml4_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        pml3[pml3_index] = pmm_allocz(1);
        if (pml3[pml3_index] == 0) {
            spinlock_release(&pagemap->lock);
            return false;
        }

        pml3[pml3_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

    if (flags & PTE_SIZE) {
        pml2[pml2_index] = paddr | flags;
        spinlock_release(&pagemap->lock);
        return true;
    }

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        pml2[pml2_index] = pmm_allocz(1);
        if (pml2[pml2_index] == 0) {
            spinlock_release(&pagemap->lock);
            return false;
        }

        pml2[pml2_index] |= (flags & MASKED_FLAGS) | PTE_WRITABLE;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = paddr | flags;

    spinlock_release(&pagemap->lock);
    return true;
}

bool vmm_unmap_page(struct pagemap* pagemap, uintptr_t vaddr) {
    spinlock_acquire(&pagemap->lock);

    size_t pml4_index = (vaddr & (0x1ffull << 39)) >> 39;
    size_t pml3_index = (vaddr & (0x1ffull << 30)) >> 30;
    size_t pml2_index = (vaddr & (0x1ffull << 21)) >> 21;
    size_t pml1_index = (vaddr & (0x1ffull << 12)) >> 12;

    uint64_t* pml4;

    if (pagemap->has_level5) {
        size_t pml5_index = (vaddr & (0x1ffull << 48)) >> 48;

        if (!(pagemap->top_level[pml5_index] & PTE_PRESENT)) {
            spinlock_release(&pagemap->lock);
            return false;
        }

        pml4 = (uint64_t*) ((pagemap->top_level[pml5_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    } else {
        pml4 = pagemap->top_level;
    }

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        spinlock_release(&pagemap->lock);
        return false;
    }

    uint64_t* pml3 = (uint64_t*) ((pml4[pml4_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    if (!(pml3[pml3_index] & PTE_PRESENT)) {
        spinlock_release(&pagemap->lock);
        return false;
    }

    uint64_t* pml2 = (uint64_t*) ((pml3[pml3_index] & ~PTE_FLAG_MASK) + HIGH_VMA);

	if (pml2[pml2_index] & PTE_SIZE) {
        pml2[pml2_index] = 0;
		invlpg(vaddr);
		spinlock_release(&pagemap->lock);
		return true;
	}

    if (!(pml2[pml2_index] & PTE_PRESENT)) {
        spinlock_release(&pagemap->lock);
        return false;
    }

    uint64_t* pml1 = (uint64_t*) ((pml2[pml2_index] & ~PTE_FLAG_MASK) + HIGH_VMA);
    pml1[pml1_index] = 0;
    invlpg(vaddr);

    spinlock_release(&pagemap->lock);
    return true;
}

void vmm_init(void) {
    klog("[vmm] initializing virtual memory manager...\n");

    pagemap_cache = slab_cache_create("pagemap cache", sizeof(struct pagemap));
    if (pagemap_cache == NULL) {
        kpanic(NULL, "failed to initialize object cache for pagemaps");
    }

    kernel_pagemap = cache_alloc_object(pagemap_cache);
    kernel_pagemap->top_level = (uint64_t*) pmm_allocz(1);
    if (kernel_pagemap->top_level == NULL) {
        kpanic(NULL, "failed to allocate memory for kernel pagemap top level");
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
        vmm_map_page(kernel_pagemap, text_addr, ALIGN_DOWN(paddr, PAGE_SIZE), PTE_PRESENT | PTE_GLOBAL);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        uintptr_t paddr = rodata_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        vmm_map_page(kernel_pagemap, rodata_addr, paddr, PTE_PRESENT | PTE_GLOBAL | PTE_NX);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        uintptr_t paddr = data_addr - kaddr_response->virtual_base + kaddr_response->physical_base;
        vmm_map_page(kernel_pagemap, data_addr, paddr, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX);
    }

    uintptr_t paddr = 0;
	for (size_t i = 1; i < 0x800; i++) {
		vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
		paddr += BIGPAGE_SIZE;
	}

    struct limine_memmap_entry** mmap_entries = memmap_request.response->entries;
    uint64_t entry_count = memmap_request.response->entry_count;

    for (size_t i = 0; i < entry_count; i++) {
        paddr = (mmap_entries[i]->base / BIGPAGE_SIZE) * BIGPAGE_SIZE;

        for (size_t j = 0; j < DIV_CEIL(mmap_entries[i]->length, BIGPAGE_SIZE); j++) {
            vmm_map_page(kernel_pagemap, paddr + HIGH_VMA, paddr,
                    PTE_PRESENT | PTE_WRITABLE | PTE_SIZE | PTE_GLOBAL | PTE_NX);
            paddr += BIGPAGE_SIZE;
        }
    }

    vmm_switch_pagemap(kernel_pagemap);
    klog("[vmm] switched to new kernel pagemap\n");

    isr_install_handler(PAGE_FAULT, false, page_fault_handler);

    klog("[vmm] initialized virtual memory manager\n");
}
