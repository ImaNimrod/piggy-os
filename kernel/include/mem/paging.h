#ifndef _KERNEL_MEM_PAGING_H
#define _KERNEL_MEM_PAGING_H

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

extern volatile struct limine_hhdm_request hhdm_request;

#define USER_START      0x0000000000001000
#define USER_END        0x0000800000000000
#define KERNEL_START    0xffff800000000000
#define KERNEL_END      0xffffffffffffffff

#define HIGH_VMA (hhdm_request.response->offset)

#define PTE_PRESENT         (1ul << 0)
#define PTE_WRITABLE        (1ul << 1)
#define PTE_USER            (1ul << 2)
#define PTE_CACHE_DISABLE   (1ul << 4)
#define PTE_SIZE            (1ul << 7)
#define PTE_GLOBAL          (1ul << 8)
#define PTE_NX              (1ul << 63)
#define PTE_WRITE_COMBINE   ((1ul << 3) | (1ul << 12))
#define PTE_FLAG_MASK       (0x8000000000000ffful)

typedef enum {
    PAGE_SIZE_4KB = 0x1000,
    PAGE_SIZE_2MB = 0x200000,
    PAGE_SIZE_1GB = 0x40000000,
} page_size_t;

struct pagemap {
    uint64_t* top_level;
    struct vma* vma_list;
    spinlock_t lock;
};

extern struct pagemap* kernel_pagemap;

struct pagemap* pagemap_create(void);
bool pagemap_destroy(struct pagemap* pagemap);
void pagemap_invalidate(uintptr_t vaddr, size_t size);
void pagemap_load(struct pagemap* pagemap);
void pagemap_map(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, uint64_t flags, page_size_t size);
bool pagemap_unmap(struct pagemap* pagemap, uintptr_t vaddr, page_size_t* out_size);
bool pagemap_remap(struct pagemap* pagemap, uintptr_t vaddr, uint64_t flags);
void pagemap_map_range(struct pagemap* pagemap, uintptr_t vaddr, uintptr_t paddr, size_t length, uint64_t flags);
bool pagemap_unmap_range(struct pagemap* pagemap, uintptr_t vaddr, size_t length);
uint64_t pagemap_get_mapping(struct pagemap* pagemap, uintptr_t vaddr, page_size_t* out_size);
void paging_init(void);

#endif /* _KERNEL_MEM_PAGING_H */
