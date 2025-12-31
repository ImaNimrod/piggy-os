#include <dev/acpi.h>
#include <utils/log.h>

#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

void acpi_early_init(void) {
    uacpi_status ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to perform early ACPI initialization via uACPI: %s", uacpi_status_to_string(ret));
    }

    klog("[acpi] finished early uACPI initialization\n");
}

void acpi_init(void) {
    uacpi_status ret;

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to load ACPI namespaces via uACPI: %s", uacpi_status_to_string(ret));
    }

    ret = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to set uACPI interrupt model to IOAPIC: %s", uacpi_status_to_string(ret));
    }

    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to initialize ACPI namespaces via uACPI: %s", uacpi_status_to_string(ret));
    }

    klog("[acpi] initialized uACPI\n");
}
