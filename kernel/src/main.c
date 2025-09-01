#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/acpi.h>
#include <dev/fbdev.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <dev/pci.h>
#include <dev/serial.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <net/netif.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER

LIMINE_REQUEST static volatile LIMINE_BASE_REVISION(3)

LIMINE_REQUEST volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

LIMINE_REQUEST volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST,
    .revision = 0,
    .flags = LIMINE_MP_X2APIC,
};

LIMINE_REQUEST volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER

NORETURN static void kernel_main(void) {
    net_init();

    pci_init();

    klog("\nhey pig...\n");

    process_create_init();

    thread_destroy(this_cpu()->running_thread);
    scheduler_await();
}

NORETURN void kernel_entry(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        cli();
        for (;;) {
            hlt();
        }
    }

    serial_init(PORT_COM1);

    pmm_init();
    slab_init();

    cmdline_parse();

    fbdev_init();

    paging_init();

    acpi_init();
    madt_parse();

    process_init();
    scheduler_init();
    timer_init();

    smp_init();

    thread_create_kernel((uintptr_t) kernel_main, NULL);
    scheduler_await();
}
