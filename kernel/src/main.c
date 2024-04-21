#include <cpu/asm.h>
#include <cpu/idt.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/acpi/acpi.h>
#include <dev/hpet.h>
#include <dev/serial.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>

static void kernel_main(void) {
    for (;;) {
        klog("bruh\n");
        sched_thread_sleep(this_cpu()->current_thread, 1e9);
    }
}

void kernel_entry(void) {
    serial_init(COM1);

    pmm_init();
    vmm_init();
    heap_init();

    acpi_init();
    hpet_init();

    sched_init();
    smp_init();

    struct thread* kthread = thread_create_kernel((uintptr_t) &kernel_main, NULL);
    sched_enqueue_thread(kthread);
    sched_await();
}
