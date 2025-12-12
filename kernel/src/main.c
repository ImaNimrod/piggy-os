#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/acpi.h>
#include <dev/block/block.h>
#include <dev/char/fb.h>
#include <dev/char/pseudo.h>
#include <dev/char/tty.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <dev/pci.h>
#include <dev/ps2.h>
#include <dev/serial.h>
#include <fs/devfs.h>
#include <fs/initrd.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <net/netif.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/random.h>
#include <utils/string.h>

__attribute__((used, section(".limine_requests_start"))) static volatile uint64_t limine_start_marker[] = LIMINE_REQUESTS_START_MARKER;

LIMINE_REQUEST static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

LIMINE_REQUEST volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

LIMINE_REQUEST volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = LIMINE_MP_REQUEST_X86_64_X2APIC,
};

LIMINE_REQUEST volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t limine_end_marker[] = LIMINE_REQUESTS_END_MARKER;

uintptr_t __stack_chk_guard;

NORETURN void __stack_chk_fail(void) {
    kpanic(NULL, true, "stack smashing detected");
}

NORETURN static void kernel_main(void) {
    devfs_init();
    tmpfs_init();

    block_init();
    net_init();

    pseudo_dev_init();
    fb_dev_init();

    pci_init();

    ps2_init();
    tty_init();

    vfs_mount(NULL, vfs_root, "/", "tmpfs");

    vfs_create(vfs_root, "/dev", VFS_TYPE_DIRECTORY, NULL);
    vfs_mount(NULL, vfs_root, "/dev", "devfs");

    struct limine_module_response* module_response = module_request.response;
    if (unlikely(module_response == NULL)) {
        kpanic(NULL, false, "missing initial ramdisk");
    }

    struct limine_file* initrd_module = NULL;
    for (uint64_t i = 0; i < module_response->module_count; i++) {
        if (strcmp(module_response->modules[i]->path, "/boot/initrd.tar") == 0) {
            initrd_module = module_response->modules[i];
            break;
        }
    }

    if (initrd_module == NULL) {
        kpanic(NULL, false, "missing initial ramdisk");
    }

    initrd_unpack(initrd_module);

    klog("\nhey pig...\n");

    process_create_init();

    scheduler_dequeue(this_cpu()->running_thread);
    thread_destroy(this_cpu()->running_thread);
    scheduler_yield(false);
    __builtin_unreachable();
}

NORETURN void kernel_entry(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        cli();
        for (;;) {
            hlt();
        }
    }

    serial_init(COM1_PORT);

    pmm_init();
    slab_init();

    cmdline_parse();

    paging_init();
    fb_dev_early_init();

    acpi_init();
    lapic_madt_parse();

    vfs_init();

    vmm_init();
    process_init();
    scheduler_init();
    timer_init();

    random_init();
    __stack_chk_guard = rand64();

    smp_init();

    struct thread* kmain_thread = thread_create_kernel((uintptr_t) kernel_main, NULL);
    scheduler_enqueue(kmain_thread);
    scheduler_await();
}
