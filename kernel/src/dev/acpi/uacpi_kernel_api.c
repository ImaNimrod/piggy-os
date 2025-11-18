#include <mem/paging.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/kernel_api.h>

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    size_t offset = addr % PAGE_SIZE_4KB;

    uintptr_t paddr = ALIGN_DOWN(addr, PAGE_SIZE_4KB);
    uintptr_t vaddr = paddr + HIGH_VMA;

    for (size_t i = 0; i < DIV_CEIL(len + offset, PAGE_SIZE_4KB); i++) {
        pagemap_map(kernel_pagemap, vaddr + (i * PAGE_SIZE_4KB), paddr + (i * PAGE_SIZE_4KB),
                PTE_PRESENT | PTE_WRITABLE | PTE_NX, PAGE_SIZE_4KB);
    }

    return (void*) (vaddr + offset);
}

void uacpi_kernel_unmap(void* addr, uacpi_size len) {
    size_t offset = (uintptr_t) addr % PAGE_SIZE_4KB;

    uintptr_t vaddr = ALIGN_DOWN((uintptr_t) addr, PAGE_SIZE_4KB);
    for (size_t i = 0; i < DIV_CEIL(len + offset, PAGE_SIZE_4KB); i++) {
        pagemap_unmap(kernel_pagemap, vaddr + (i * PAGE_SIZE_4KB));
    }
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
    klog("[acpi] %s", str);
}
