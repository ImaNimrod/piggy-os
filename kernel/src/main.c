#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/char/fbdev.h>
#include <dev/char/pseudo.h>
#include <dev/char/tty.h>
#include <dev/hpet.h>
#include <dev/pci.h>
#include <dev/ps2.h>
#include <dev/serial.h>
#include <fs/devfs.h>
#include <fs/initrd.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <sys/time.h>
#include <utils/cmdline.h>
#include <utils/panic.h>
#include <utils/random.h>

uintptr_t __stack_chk_guard;

__attribute__((noreturn)) void __stack_chk_fail(void) {
    kpanic(NULL, true, "stack smashing detected");
}

static void kernel_main(void) {
    time_init();

    vfs_init();
    devfs_init();
    tmpfs_init();

    vfs_mount(vfs_root, NULL, "/", "tmpfs");

    vfs_create(vfs_root, "/dev", S_IFDIR);
    vfs_mount(vfs_root, NULL, "/dev", "devfs");

    pseudo_init();
    fbdev_init();
    tty_init();
    ps2_init();

    if (!initrd_unpack()) {
        kpanic(NULL, false, "failed to unpack initrd");
    }

    pci_init();

    if (!process_create_init()) {
        kpanic(NULL, false, "failed to create init process");
    }

    vmm_unmap_code_after_init();

    sched_thread_dequeue(this_cpu()->running_thread);
    sched_yield();
}

__attribute__((section(".unmap_after_init")))
void kernel_entry(void) {
    pmm_init();
    slab_init();

    cmdline_parse();

    vmm_init();

    acpi_init();
    hpet_init();

    random_init();
    __stack_chk_guard = fast_rand();

    process_init();
    sched_init();
    smp_init();

    struct thread* main_thread = thread_create(kernel_process, (uintptr_t) &kernel_main, NULL, NULL, NULL, false);
    sched_thread_enqueue(main_thread);
    sched_await();
}
