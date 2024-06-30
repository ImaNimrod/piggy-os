#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
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
#include <utils/log.h>

static void kernel_main(void) {
    vfs_init();
    devfs_init();
    tmpfs_init();

    vfs_mount(vfs_root, NULL, "/", "tmpfs");

    vfs_create(vfs_root, "/dev", VFS_NODE_DIRECTORY);
    vfs_mount(vfs_root, NULL, "/dev", "devfs");

    fbdev_init();
    streams_init();

    initrd_unpack();

    if (!process_create_init()) {
        kpanic(NULL, "failed to create init process");
    }

    sched_thread_destroy(this_cpu()->running_thread);
    sched_yield();
}

void kernel_entry(void) {
    serial_init(COM1);

    pmm_init();
    slab_init();
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
