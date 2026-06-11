#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/acpi.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <uacpi/event.h>
#include <uacpi/kernel_api.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/utilities.h>
#include <utils/cmdline.h>
#include <utils/log.h>

#define EARLY_BUFFER_SIZE (PAGE_SIZE_4KB * 4)

static uintptr_t early_tables_paddr;

static void do_poweroff(uacpi_handle ctx) {
    (void) ctx;

    klog("[acpi] power button pressed, shutting down...\n");

    if (acpi_shutdown() < 0) {
        klog("[acpi] shutdown failed\n");
    }
}

static uacpi_interrupt_ret handle_power_button(uacpi_handle ctx) {
    uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, do_poweroff, ctx);
    return UACPI_INTERRUPT_HANDLED;
}

int acpi_reboot(void) {
    uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        return -EIO;
    }

    cli();
    smp_halt_other_cpus();
    uacpi_reboot();

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

int acpi_shutdown(void) {
    uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        return -EIO;
    }

    cli();
    smp_halt_other_cpus();

    uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

void acpi_early_init(void) {
    early_tables_paddr = pmm_alloc_zero(EARLY_BUFFER_SIZE / PAGE_SIZE_4KB);

    uacpi_status ret = uacpi_setup_early_table_access((void*) (early_tables_paddr + HIGH_VMA), EARLY_BUFFER_SIZE);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to initialize ACPI via UACPI: %s", uacpi_status_to_string(ret));
    }

    klog("[acpi] finished early uACPI initialization\n");
}

void acpi_init(void) {
    bool noacpi = cmdline_get("noacpi") != NULL;
    if (noacpi) {
        klog("[acpi] 'noacpi' argument found, performing only limited ACPI initialization\n");
    }

    uacpi_status ret;

    ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to initialize uACPI: %s", uacpi_status_to_string(ret));
    }

    pmm_free(early_tables_paddr, EARLY_BUFFER_SIZE / PAGE_SIZE_4KB);

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to load ACPI namespaces via uACPI: %s", uacpi_status_to_string(ret));
    }

    if (!noacpi) {
        ret = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
        if (uacpi_unlikely_error(ret)) {
            kpanic(NULL, false, "failed to set uACPI interrupt model to IOAPIC: %s", uacpi_status_to_string(ret));
        }

        ret = uacpi_namespace_initialize();
        if (uacpi_unlikely_error(ret)) {
            kpanic(NULL, false, "failed to initialize ACPI namespaces via uACPI: %s", uacpi_status_to_string(ret));
        }

        ret = uacpi_finalize_gpe_initialization();
        if (uacpi_unlikely_error(ret)) {
            kpanic(NULL, false, "ACPI GPE initialization error: %s", uacpi_status_to_string(ret));
        }

        uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, handle_power_button, UACPI_NULL);
    }

    klog("[acpi] initialized uACPI\n");
}
