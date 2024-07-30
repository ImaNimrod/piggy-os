#include <dev/acpi/acpi.h>
#include <dev/acpi/madt.h>
#include <limine.h>
#include <mem/vmm.h>
#include <utils/log.h>
#include <utils/string.h>

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    char reserved[3];
} __attribute__((packed));

static struct rsdp* rsdp;
static struct acpi_sdt* rsdt;
static bool use_xsdt;

static bool verify_checksum(struct acpi_sdt* sdt) {
    uint8_t sum = 0;

    for (size_t i = 0; i < sdt->length; i++) {
        sum += ((uint8_t*) sdt)[i];
    }

    return sum == 0;
}

struct acpi_sdt* acpi_find_sdt(const char signature[static 4]) {
    struct acpi_sdt* sdt = NULL;
    size_t entry_count = (rsdt->length - sizeof(struct acpi_sdt)) / (use_xsdt ? 8 : 4);

    for (size_t i = 0; i < entry_count; i++) {
        if (use_xsdt) {
            sdt = (struct acpi_sdt*) (*((uint64_t*) (rsdt + 1) + i) + HIGH_VMA);
        } else {
            sdt = (struct acpi_sdt*) (*((uint32_t*) (rsdt + 1) + i) + HIGH_VMA);
        }

        if (memcmp(sdt->signature, signature, 4)) {
            continue;
        }

        if (!verify_checksum(sdt)) {
            kpanic(NULL, true, "APCI table '%s' has an invalid checksum", signature);
            continue;
        }

        return sdt;
    }

    return NULL;
}

void acpi_init(void) {
    struct limine_rsdp_response* rsdp_response = rsdp_request.response;
    rsdp = rsdp_response->address;

    use_xsdt = rsdp->revision >= 2 && rsdp->xsdt_addr;

    if (use_xsdt) {
        rsdt = (struct acpi_sdt*) (rsdp->xsdt_addr + HIGH_VMA);
        if (memcmp(rsdt, "XSDT", 4) || !verify_checksum(rsdt)) {
            kpanic(NULL, false, "XSDT corrupted or not present");
        }
    } else {
        rsdt = (struct acpi_sdt*) (rsdp->rsdt_addr + HIGH_VMA);
        if (memcmp(rsdt, "RSDT", 4) || !verify_checksum(rsdt)) {
            kpanic(NULL, false, "RSDT corrupted or not present");
        }
    }

    klog("[acpi] initialized using revision %s\n", use_xsdt ? "2.0" : "1.0");

    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (fadt != NULL && fadt->length >= 116) {
        uint32_t fadt_flags = *((uint32_t*) fadt + 28);

        if (fadt_flags & (1 << 20)) {
            kpanic(NULL, false, "unable to use reduced ACPI systems");
        }
    }

    madt_init();
}
