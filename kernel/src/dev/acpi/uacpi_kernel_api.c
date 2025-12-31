#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/pci.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/semaphore.h>
#include <utils/spinlock.h>

#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>

extern struct limine_rsdp_request rsdp_request;

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    *out_rsdp_address = (uintptr_t) rsdp_request.response->address - HIGH_VMA;
    return UACPI_STATUS_OK;
}

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

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
    uint64_t value = ((uint64_t) address.segment << 48) | ((uint64_t) address.bus << 32) | ((uint64_t) address.device << 16) | ((uint64_t) address.function);
    *out_handle = (uacpi_handle) value;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    (void) handle;
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    *value = pci_raw_read(segment, bus, slot, function, offset, 1);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    *value = pci_raw_read(segment, bus, slot, function, offset, 2);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    *value = pci_raw_read(segment, bus, slot, function, offset, 4);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    pci_raw_write(segment, bus, slot, function, offset, value, 1);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    pci_raw_write(segment, bus, slot, function, offset, value, 2);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
    uint64_t address = (uint64_t) handle;
    uint16_t segment = (address >> 48) & 0xffff;
    uint8_t bus = address >> 32;
    uint8_t slot = (address >> 16) & 0xffff;
    uint8_t function = address & 0xffff;

    pci_raw_write(segment, bus, slot, function, offset, value, 4);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size size, uacpi_handle* out_handle) {
    (void) size;

    *out_handle = (uacpi_handle) base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    (void) handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out_value) {
    *out_value = inb((uacpi_io_addr) handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out_value) {
    *out_value = inw((uacpi_io_addr) handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out_value) {
    *out_value = inl((uacpi_io_addr) handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    outb((uacpi_io_addr) handle + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    outw((uacpi_io_addr) handle + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    outl((uacpi_io_addr) handle + offset, in_value);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

void* uacpi_kernel_alloc_zeroed(uacpi_size size) {
    return kmalloc(size);
}

void uacpi_kernel_free(void* ptr) {
    if (ptr != NULL) {
        kfree(ptr);
    }
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return S_TO_NS(time_monotonic.tv_sec) + time_monotonic.tv_nsec;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    (void) usec;
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    struct timespec ts = {
        .tv_sec = msec / 1000,
        .tv_nsec = (msec % 1000) * 1000000L,
    };

    scheduler_sleep(this_cpu()->running_thread, &ts);
}

uacpi_handle uacpi_kernel_create_mutex(void) {
    return (uacpi_handle) kmalloc(sizeof(spinlock_t));
}

void uacpi_kernel_free_mutex(uacpi_handle mutex) {
    kfree((spinlock_t*) mutex);
}

uacpi_handle uacpi_kernel_create_event(void) {
    semaphore_t* event = kmalloc(sizeof(semaphore_t));
    if (likely(event != NULL)) {
        semaphore_init(event, 1);
    }
    return event;
}

void uacpi_kernel_free_event(uacpi_handle event) {
    kfree((semaphore_t*) event);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    //return (uacpi_thread_id) this_cpu()->running_thread;
    return 0;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle mutex, uacpi_u16 msec) {
    (void) msec;

    spinlock_acquire((spinlock_t*) mutex);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle mutex) {
    spinlock_release((spinlock_t*) mutex);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle event, uacpi_u16 msec) {
    (void) msec;

    semaphore_wait((semaphore_t*) event);
    return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle event) {
    semaphore_signal((semaphore_t*) event);
}

void uacpi_kernel_reset_event(uacpi_handle event) {
    semaphore_reset((semaphore_t*) event);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* request) {
    if (request->type == UACPI_FIRMWARE_REQUEST_TYPE_FATAL) {
        klog("[acpi] fatal firmware error type: %u, code: %u, arg: 0x%lx\n",
                request->fatal.type, request->fatal.code, request->fatal.arg);
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
    (void) irq;
    (void) handler;
    (void) ctx;
    (void) out_irq_handle;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
    (void) handler;
    (void) irq_handle;
    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    return (uacpi_handle) kmalloc(sizeof(spinlock_t));
}

void uacpi_kernel_free_spinlock(uacpi_handle lock) {
    kfree((spinlock_t*) lock);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle lock) {
    return spinlock_acquire_irqsave((spinlock_t*) lock);
}

void uacpi_kernel_unlock_spinlock(uacpi_handle lock, uacpi_cpu_flags int_state) {
    spinlock_release_irqsave((spinlock_t*) lock, int_state);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
    (void) type;
    (void) handler;
    (void) ctx;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_UNIMPLEMENTED;
}
