#include <mem/pmm.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/spinlock.h>
#include <utils/string.h>

volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static uint8_t* pmm_bitmap = NULL;
static spinlock_t pmm_lock = {0};
static size_t usable_pages = 0;
static size_t reserved_pages = 0;
static size_t used_pages = 0;
static size_t last_used_index = 0;
static size_t highest_page_index = 0;

static char* memmap_type_str(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return "usable";
        case LIMINE_MEMMAP_RESERVED: return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "acpi reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS: return "acpi non-volatile storage";
        case LIMINE_MEMMAP_BAD_MEMORY: return "bad memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES: return "kernel and modules";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "framebuffer";
        default: return "unknown type";
    }
}

#define PMM_BITMAP_SET(bit) (pmm_bitmap[(bit) / 8] |= (1 << ((bit) % 8)))
#define PMM_BITMAP_RESET(bit) (pmm_bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define PMM_BITMAP_TEST(bit) (pmm_bitmap[(bit) / 8] & (1 << ((bit) % 8)))

static uintptr_t alloc_first_fit_from_last(size_t pages, uint64_t last_limit) {
    size_t p = 0;

    while (last_used_index < last_limit) {
        last_used_index++;
        if (!PMM_BITMAP_TEST(last_used_index)) {
            if (++p == pages) {
                size_t page = last_used_index - pages;

                for (size_t i = page; i < last_used_index; i++) {
                    PMM_BITMAP_SET(i);
                }

                return page * PAGE_SIZE;
            }
        } else {
            p = 0;
        }
    }

    return (uintptr_t) -1;
}

uintptr_t pmm_alloc(size_t pages) {
    spinlock_acquire(&pmm_lock);

    size_t last = last_used_index;
    uintptr_t ret = alloc_first_fit_from_last(pages, highest_page_index);

    if (ret == (uintptr_t) -1) {
        last_used_index = 0;
        ret = alloc_first_fit_from_last(pages, last);
    }

    if (ret == (uintptr_t) -1) {
        kpanic(NULL, "pmm unable to allocate %lu physical pages", pages);
    }

    used_pages += pages;
    memset((void*) (ret + HIGH_VMA), 0, PAGE_SIZE);

    spinlock_release(&pmm_lock);
    return ret;
}

void pmm_free(uintptr_t addr, size_t pages) {
    spinlock_acquire(&pmm_lock);
    size_t page = (uint64_t) addr / PAGE_SIZE;

    for (size_t i = page; i < page + pages; i++) {
        PMM_BITMAP_RESET(i);
    }

    used_pages -= pages;
    spinlock_release(&pmm_lock);
}

void pmm_init(void) {
    struct limine_memmap_response* memmap_response = memmap_request.response;
    struct limine_memmap_entry** entries = memmap_response->entries;

    klog("[pmm] initializing physical memory manager...\n");

    uint64_t highest_addr = 0;

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        klog("[pmm] memory map entry: base=0x%lx, length=0x%lx, type: %s\n",
                entry->base, entry->length, memmap_type_str(entry->type));

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_pages += DIV_CEIL(entry->length, PAGE_SIZE);
            highest_addr = MAX(highest_addr, entry->base + entry->length);
        } else {
            reserved_pages += DIV_CEIL(entry->length, PAGE_SIZE);
        }
    }

    highest_page_index = highest_addr / PAGE_SIZE;
    uint64_t pmm_bitmap_size = ALIGN_UP(highest_page_index / 8, PAGE_SIZE);

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (entry->length >= pmm_bitmap_size) {
            pmm_bitmap = (uint8_t*) (entry->base + HIGH_VMA);
            memset(pmm_bitmap, 0xff, pmm_bitmap_size);

            entry->length -= pmm_bitmap_size;
            entry->base += pmm_bitmap_size;

            break;
        }
    }

    for (size_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
            PMM_BITMAP_RESET((entry->base + j) / PAGE_SIZE);
        }
    }

    klog("[pmm] usable memory: %luMiB reserved memory: %luMiB\n", (usable_pages * PAGE_SIZE) >> 20, (reserved_pages * PAGE_SIZE) >> 20);
    klog("[pmm] initialized physical memory manager\n");
}
