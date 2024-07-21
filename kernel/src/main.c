#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/char/console.h>
#include <dev/char/fbdev.h>
#include <dev/char/streams.h>
#include <dev/hpet.h>
#include <dev/serial.h>
#include <fs/devfs.h>
#include <fs/initrd.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/random.h>

static void kernel_main(void) {
    random_init();

    vfs_init();
    devfs_init();
    tmpfs_init();

    vfs_mount(vfs_root, NULL, "/", "tmpfs");

    vfs_create(vfs_root, "/dev", S_IFDIR);
    vfs_mount(vfs_root, NULL, "/dev", "devfs");

    console_init();
    fbdev_init();
    streams_init();

    if (!initrd_unpack()) {
        kpanic(NULL, "failed to unpack initrd");
    }

    if (!process_create_init()) {
        kpanic(NULL, "failed to create init process");
    }

    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}

void kernel_entry(void) {
    if (cmdline_early_get_klog()) {
        serial_init(COM1);
    }

    pmm_init();
    slab_init();

    cmdline_parse();

    vmm_init();

    acpi_init();
    hpet_init();

    process_init();
    sched_init();
    smp_init();

    struct thread* main_thread = thread_create(kernel_process, (uintptr_t) &kernel_main, NULL, NULL, NULL, false);
    sched_thread_enqueue(main_thread);
    sched_await();
}
