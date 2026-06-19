#include <mem/paging.h>
#include <mem/pmm.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define INVALID_PADDR (uintptr_t) -1

extern struct limine_memmap_request memmap_request;

static uint64_t* pmm_bitmap;
static spinlock_t pmm_lock;
static size_t reserved_pages;
static size_t usable_pages;
static size_t highest_page_index;
static size_t last_used_index;

static inline const char* memmap_type_str(uint64_t memmap_type) {
    switch (memmap_type) {
        case LIMINE_MEMMAP_USABLE: return "usable";
        case LIMINE_MEMMAP_RESERVED: return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "APCI reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS: return "ACPI non-volatile storage";
        case LIMINE_MEMMAP_BAD_MEMORY: return "bad memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader reclaimable";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "kernel and modules";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "framebuffer";
        case LIMINE_MEMMAP_RESERVED_MAPPED: return "reserved mapped";
        default: return "unknown type";
    }
}

static uintptr_t inner_alloc(size_t pages, uint64_t last_limit) {
    for (size_t start = last_used_index; start + pages <= last_limit; start++) {
        if (BITMAP_TEST(pmm_bitmap, start)) {
            continue;
        }

        bool contiguous = true;

        for (size_t i = 0; i < pages; i++) {
            if (BITMAP_TEST(pmm_bitmap, start + i)) {
                contiguous = false;
                start += i;
                break;
            }
        }

        if (!contiguous) {
            continue;
        }
        for (size_t i = 0; i < pages; i++) {
            BITMAP_SET(pmm_bitmap, start + i);
        }

        last_used_index = start + pages;
        return start * PAGE_SIZE_4KB;
    }

    return INVALID_PADDR;
}

uintptr_t pmm_alloc(size_t page_count) {
    spinlock_acquire(&pmm_lock);

    size_t last = last_used_index;

    uintptr_t ret = inner_alloc(page_count, highest_page_index);
    if (ret == INVALID_PADDR) {
        last_used_index = 0;
        ret = inner_alloc(page_count, last);
    }

    if (ret == INVALID_PADDR) {
        kpanic(NULL, true, "OUT OF MEMORY!");
    }

    spinlock_release(&pmm_lock);
    return ret;
}

uintptr_t pmm_alloc_zero(size_t page_count) {
    uintptr_t ret = pmm_alloc(page_count);
    memset64((void*) (ret + HIGH_VMA), 0, (PAGE_SIZE_4KB * page_count) >> 3);
    return ret;
}

void pmm_free(uintptr_t paddr, size_t page_count) {
    size_t page = paddr / PAGE_SIZE_4KB;
    if ((page + page_count) > highest_page_index) {
        kpanic(NULL, true, "tried to free physical pages that are outside bounds of physical memory");
    }

    spinlock_acquire(&pmm_lock);

    for (size_t i = page; i < page + page_count; i++) {
        BITMAP_CLEAR(pmm_bitmap, i);
    }

    spinlock_release(&pmm_lock);
}

void pmm_reserve_mmio_space(uintptr_t paddr, size_t page_count) {
    size_t page = paddr / PAGE_SIZE_4KB;
    if ((page + page_count) > highest_page_index) {
        return;
    }

    spinlock_acquire(&pmm_lock);

    for (size_t i = page; i < page + page_count; i++) {
        BITMAP_SET(pmm_bitmap, i);
    }

    spinlock_release(&pmm_lock);
}

void pmm_init(void) {
    struct limine_memmap_response* memmap_response = memmap_request.response;

    klog("[pmm] parsing memory map:\n");

    uintptr_t highest_paddr = 0;

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_response->entries[i];

        klog("- memory map entry #%02u: base=0x%016lx, length=0x%016lx, type: %s\n",
                i, entry->base, entry->length, memmap_type_str(entry->type));

        size_t page_count = entry->length / PAGE_SIZE_4KB;
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_pages += page_count;
        } else {
            reserved_pages += page_count;
        }

        uintptr_t end = entry->base + entry->length;
        if (end > highest_paddr)
            highest_paddr = end;
    }

    highest_page_index = highest_paddr / PAGE_SIZE_4KB;
    size_t pmm_bitmap_size  = ALIGN_UP(DIV_CEIL(highest_page_index, 8), PAGE_SIZE_4KB);

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE || entry->length < pmm_bitmap_size) {
            continue;
        }

        pmm_bitmap = (uint64_t*) (entry->base + HIGH_VMA);
        memset64(pmm_bitmap, 0xffffffffffffffff, pmm_bitmap_size >> 3);
        break;
    }

    if (unlikely(pmm_bitmap == NULL)) {
        kpanic(NULL, false, "unable to find suitable memory region for PMM bitmap");
    }

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uintptr_t base = entry->base;
            size_t length = entry->length;

            if (((uintptr_t) pmm_bitmap - HIGH_VMA) >= base && ((uintptr_t) pmm_bitmap - HIGH_VMA) < base + length) {
                base += pmm_bitmap_size;
                length -= pmm_bitmap_size;
            }

            for (uint64_t j = 0; j < length; j += PAGE_SIZE_4KB) {
                BITMAP_CLEAR(pmm_bitmap, (base + j) / PAGE_SIZE_4KB);
            }
        }
    }

    klog("[pmm] usable memory: %zuMiB | reserved memory: %zuMiB\n",
            (usable_pages * PAGE_SIZE_4KB) >> 20, (reserved_pages * PAGE_SIZE_4KB) >> 20);
    klog("[pmm] initialized physical memory manager\n");
}
