#include <cpu/asm.h>
#include <cpu/ioapic.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/pci.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/semaphore.h>
#include <utils/spinlock.h>

struct uacpi_irq_context {
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
};

struct uacpi_work {
    uacpi_work_handler handler;
    uacpi_handle ctx;
    struct uacpi_work* next;
};

struct uacpi_work_context {
    semaphore_t semaphore;
    struct thread* thread;
    struct uacpi_work* queue;
    spinlock_t queue_lock;
};

extern struct limine_rsdp_request rsdp_request;

static struct uacpi_work_context gpe_work;
static struct uacpi_work_context notification_work;

static void do_work(struct uacpi_work_context* context) {
    for (;;) {
        semaphore_wait(&context->semaphore);

        struct uacpi_work* work = NULL;

        bool int_state = spinlock_acquire_irqsave(&context->queue_lock);
        if (context->queue) {
            work = context->queue;
            context->queue = work->next;
        }
        spinlock_release_irqsave(&context->queue_lock, int_state);

        if (!work) {
            continue;
        }

        work->handler(work->ctx);
        kfree(work);
    }
}

static void do_gpe_work(void) {
    do_work(&gpe_work);
}

static void do_notification_work(void) {
    do_work(&notification_work);
}

static void work_await(struct uacpi_work_context* context) {
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = MS_TO_NS(100),
    };

    for (;;) {
        bool int_state = spinlock_acquire_irqsave(&context->queue_lock);
        bool empty = context->queue == NULL;
        spinlock_release_irqsave(&context->queue_lock, int_state);

        if (empty) {
            return;
        }

        scheduler_sleep(this_cpu()->scheduler.current_thread, &delay);
	}
}

static void work_init(struct uacpi_work_context* context, void (*proc)(void)) {
    semaphore_init(&context->semaphore, 0);
    spinlock_init(&context->queue_lock);

    context->thread = thread_create_kernel((uintptr_t) proc, NULL);
    if (unlikely(!context->thread)) {
        kpanic(NULL, false, "failed to create uACPI worker thread");
    }
}

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

uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {
    if (current_init_lvl != UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED) {
        return UACPI_STATUS_OK;
    }

    work_init(&gpe_work, do_gpe_work);
    work_init(&notification_work, do_notification_work);

    return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize(void) {}

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
    return kmallocz(size);
}

void uacpi_kernel_free(void* ptr) {
    kfree(ptr);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    struct timespec boottime = timer_time_from_boot();
    return S_TO_NS(boottime.tv_sec) + boottime.tv_nsec;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    timer_wait_ns(usec * 1000);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    struct timespec ts = {
        .tv_sec = msec / 1000,
        .tv_nsec = (msec % 1000) * 1000000L,
    };

    scheduler_sleep(this_cpu()->scheduler.current_thread, &ts);
}

uacpi_handle uacpi_kernel_create_mutex(void) {
    mutex_t* mutex = kmalloc(sizeof(mutex_t));
    if (likely(mutex)) {
        mutex_init(mutex);
    }
    return (uacpi_handle) mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle mutex) {
    kfree((mutex_t*) mutex);
}

uacpi_handle uacpi_kernel_create_event(void) {
    semaphore_t* event = kmalloc(sizeof(semaphore_t));
    if (likely(event)) {
        semaphore_init(event, 1);
    }
    return event;
}

void uacpi_kernel_free_event(uacpi_handle event) {
    kfree((semaphore_t*) event);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return (uacpi_thread_id) this_cpu()->scheduler.current_thread;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
    bool int_state = get_interrupt_state();
    cli();
    return int_state;
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {
    if (state) {
        sti();
    }
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle mutex, uacpi_u16 msec) {
    (void) msec;

    mutex_acquire((mutex_t*) mutex);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle mutex) {
    mutex_release((mutex_t*) mutex);
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

static void uacpi_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct uacpi_irq_context* irq_context = arg;
    irq_context->handler(irq_context->ctx);
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
    (void) out_irq_handle;

    struct uacpi_irq_context* irq_context = kmalloc(sizeof(struct uacpi_irq_context));
    if (unlikely(!irq_context)) {
        kpanic(NULL, true, "failed to allocate memory for uACPI IRQ context");
    }
    irq_context->handler = handler;
    irq_context->ctx = ctx;

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, true, "failed to allocate IRQ vector for uACPI interrupt");
    }
    isr_register_handler(vector, uacpi_irq_handler, irq_context);

    ioapic_redirect_irq(irq, vector);
    ioapic_set_irq_mask(irq, false);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
    (void) handler;
    (void) irq_handle;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_t* lock = kmalloc(sizeof(spinlock_t));
    if (likely(lock)) {
        spinlock_init(lock);
    }
    return (uacpi_handle) lock;
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
    struct uacpi_work* work = kmalloc(sizeof(struct uacpi_work));
    if (unlikely(!work)) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }
    work->ctx = ctx;
    work->handler = handler;

    struct uacpi_work_context* context;
    if (type == UACPI_WORK_GPE_EXECUTION) {
        context = &gpe_work;
    } else if (type == UACPI_WORK_NOTIFICATION) {
        context = &notification_work;
    } else {
        kpanic(NULL, false, "unknown work type: %d", type);
    }

    bool int_state = spinlock_acquire_irqsave(&context->queue_lock);
    work->next = context->queue;
    context->queue = work;
    spinlock_release_irqsave(&context->queue_lock, int_state);

    semaphore_signal(&context->semaphore);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    work_await(&gpe_work);
    work_await(&notification_work);
    return UACPI_STATUS_OK;
}
