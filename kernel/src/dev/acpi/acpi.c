#include <dev/acpi.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <utils/log.h>
#include <utils/macros.h>

#include <uacpi/kernel_api.h>
#include <uacpi/uacpi.h>

extern struct limine_rsdp_request rsdp_request;

#define EARLY_BUFFER_SIZE (PAGE_SIZE_4KB * 4)

static void* early_tables_buffer;
static uintptr_t rsdp_paddr;

void acpi_init(void) {
    struct limine_rsdp_response* rsdp_response = rsdp_request.response;
    rsdp_paddr = (uintptr_t) rsdp_response->address - HIGH_VMA;

    early_tables_buffer = (void*) (pmm_alloc_zero(EARLY_BUFFER_SIZE / PAGE_SIZE_4KB) + HIGH_VMA);

    uacpi_status ret = uacpi_setup_early_table_access(early_tables_buffer, EARLY_BUFFER_SIZE);
    if (uacpi_unlikely_error(ret)) {
        kpanic(NULL, false, "failed to initialize ACPI via UACPI: %s", uacpi_status_to_string(ret));
    }
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    *out_rsdp_address = rsdp_paddr;
    return UACPI_STATUS_OK;
}
