#include <limine.h>
#include <mem/pmm.h>
#include <types.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static uint8_t* pmm_bitmap = NULL;

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

static void* alloc_first_fit_from_last(size_t pages, uint64_t last_limit) {
    size_t p = 0;

    while (last_used_index < last_limit) {
        last_used_index++;
        if (!PMM_BITMAP_TEST(last_used_index)) {
            if (++p == pages) {
                size_t page = last_used_index - pages;

                for (size_t i = page; i < last_used_index; i++) {
                    PMM_BITMAP_SET(i);
                }

                return (void*) (page * 4096);
            }
        } else {
            p = 0;
        }
    }

    return NULL;
}

void* pmm_alloc(size_t pages) {
    size_t last = last_used_index;
    void* ret = alloc_first_fit_from_last(pages, highest_page_index);

    if (ret == NULL) {
        last_used_index = 0;
        ret = alloc_first_fit_from_last(pages, last);
    }

    used_pages += pages;

    return ret;
}

void pmm_free(void* addr, size_t pages) {
    size_t page = (uint64_t) addr / 4096;

    for (size_t i = page; i < page + pages; i++) {
        PMM_BITMAP_RESET(i);
    }

    used_pages -= pages;
}

/* TODO: replace 4096 with PAGE_SIZE macro in vmm */
/* TODO: move hhdm request to vmm */
void pmm_init(void) {
    struct limine_memmap_response* memmap = memmap_request.response;
    struct limine_hhdm_response* hhdm = hhdm_request.response;
    struct limine_memmap_entry** entries = memmap->entries;

    uint64_t highest_addr = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        klog("[pmm] memory map entry: base=0x%lx, length=0x%lx, type: %s\n",
                entry->base, entry->length, memmap_type_str(entry->type));

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_pages += DIV_CEIL(entry->length, 4096);
            highest_addr = MAX(highest_addr, entry->base + entry->length);
        } else {
            reserved_pages += DIV_CEIL(entry->length, 4096);
        }
    }

    highest_page_index = highest_addr / 4096;
    uint64_t pmm_bitmap_size = ALIGN_UP(highest_page_index / 8, 4096);

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (entry->length >= pmm_bitmap_size) {
            pmm_bitmap = (uint8_t*) (entry->base + KERNEL_HIGH_VMA);
            memset(pmm_bitmap, 0xff, pmm_bitmap_size);

            entry->length -= pmm_bitmap_size;
            entry->base += pmm_bitmap_size;

            break;
        }
    }

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        for (uint64_t j = 0; j < entry->length; j += 4096) {
            PMM_BITMAP_RESET((entry->base + j) / 4096);
        }
    }

    klog("[pmm] usable memory: %luMiB\n", (usable_pages * 4096) >> 20);
    klog("[pmm] reserved memory: %luMiB\n", (reserved_pages * 4096) >> 20);
}
