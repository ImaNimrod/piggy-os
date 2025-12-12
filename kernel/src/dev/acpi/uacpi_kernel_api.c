#include <mem/paging.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/kernel_api.h>

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    size_t offset = addr & (PAGE_SIZE_4KB - 1);

    uintptr_t paddr = ALIGN_DOWN(addr, PAGE_SIZE_4KB);
    uintptr_t vaddr = paddr + HIGH_VMA;

    pagemap_map_range(kernel_pagemap, vaddr, paddr, ALIGN_UP(len + offset, PAGE_SIZE_4KB), PTE_PRESENT | PTE_WRITABLE | PTE_NX);

    return (void*) (vaddr + offset);
}

void uacpi_kernel_unmap(void* addr, uacpi_size len) {
    size_t offset = (uintptr_t) addr & (PAGE_SIZE_4KB - 1);
    uintptr_t vaddr = ALIGN_DOWN((uintptr_t) addr, PAGE_SIZE_4KB);
    size_t size = ALIGN_UP(len + offset, PAGE_SIZE_4KB);

    pagemap_unmap_range(kernel_pagemap, vaddr, size);
    pagemap_invalidate(vaddr, size);
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
    const char* level_str;

    switch (level) {
        case UACPI_LOG_DEBUG:
            level_str = "debug";
            break;
        case UACPI_LOG_TRACE:
            level_str = "trace";
            break;
        case UACPI_LOG_INFO:
            level_str = "info";
            break;
        case UACPI_LOG_WARN:
            level_str = "warn";
            break;
        case UACPI_LOG_ERROR:
            level_str = "error";
            break;
        default:
            __builtin_unreachable();
    }

    klog("[acpi][%s] %s", level_str, str);
}
