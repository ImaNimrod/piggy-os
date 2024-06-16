#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/hpet.h>
#include <dev/serial.h>
#include <fs/initramfs.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/log.h>

static void kernel_main(void) {
    vfs_init();
    tmpfs_init();

    vfs_mount(vfs_get_root(), NULL, "/", "tmpfs");

    initramfs_init();

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

    sched_init();
    smp_init();

    struct thread* kthread = thread_create_kernel((uintptr_t) &kernel_main, NULL);
    sched_thread_enqueue(kthread);
    sched_await();
}
