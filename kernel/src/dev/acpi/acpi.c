#include <dev/acpi.h>
#include <limine.h>
#include <mem/paging.h>
#include <stdbool.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

extern struct limine_rsdp_request rsdp_request;

static struct rsdp* rsdp;
static struct acpi_sdt* rsdt;
static size_t rsdt_entry_count;
static bool use_acpi_rev2;

static bool verify_checksum(struct acpi_sdt* sdt) {
    register uint8_t sum = 0;

    for (size_t i = 0; i < sdt->length; i++) {
        sum += ((uint8_t*) sdt)[i];
    }

    return sum == 0;
}

struct acpi_sdt* acpi_find_sdt(const char signature[static 4]) {
    struct acpi_sdt* sdt = NULL;

    for (size_t i = 0; i < rsdt_entry_count; i++) {
        if (use_acpi_rev2) {
            sdt = (struct acpi_sdt*) (*((uint64_t*) (rsdt + 1) + i) + HIGH_VMA);
        } else {
            sdt = (struct acpi_sdt*) (*((uint32_t*) (rsdt + 1) + i) + HIGH_VMA);
        }

        if (memcmp(sdt->signature, signature, 4)) {
            continue;
        }

        if (!verify_checksum(sdt)) {
            kpanic(NULL, false, "APCI table '%s' has an invalid checksum", signature);
        }

        return sdt;
    }

    return NULL;
}

void acpi_init(void) {
    struct limine_rsdp_response* rsdp_response = rsdp_request.response;

    rsdp = (struct rsdp*) (rsdp_response->address + HIGH_VMA);

    use_acpi_rev2 = rsdp->revision >= 2 && rsdp->xsdt_addr;

    if (use_acpi_rev2) {
        rsdt = (struct acpi_sdt*) (rsdp->xsdt_addr + HIGH_VMA);
        if (unlikely(memcmp(rsdt, "XSDT", 4) || !verify_checksum(rsdt))) {
            kpanic(NULL, false, "XSDT corrupted or not present");
        }
    } else {
        rsdt = (struct acpi_sdt*) (rsdp->rsdt_addr + HIGH_VMA);
        if (unlikely(memcmp(rsdt, "RSDT", 4) || !verify_checksum(rsdt))) {
            kpanic(NULL, false, "RSDT corrupted or not present");
        }
    }

    rsdt_entry_count = (rsdt->length - sizeof(struct acpi_sdt)) / (use_acpi_rev2 ? sizeof(uint64_t) : sizeof(uint32_t));
    for (size_t i = 0; i < rsdt_entry_count; i++) {
        uintptr_t table_paddr = (uintptr_t) *((uint64_t*) (rsdt + 1) + i);
        pagemap_map(kernel_pagemap, table_paddr + HIGH_VMA, table_paddr, PTE_PRESENT | PTE_CACHE_DISABLE | PTE_NX, PAGE_SIZE_4KB);
    }

    struct acpi_sdt* fadt = acpi_find_sdt("FACP");
    if (unlikely(fadt != NULL && fadt->length >= 116)) {
        uint32_t fadt_flags = *((uint32_t*) fadt + 28);

        if (fadt_flags & (1 << 20)) {
            kpanic(NULL, false, "unable to use reduced ACPI systems");
        }
    }

    klog("[acpi] ACPI subsystem initialized (revision %s)\n", use_acpi_rev2 ? "2.0" : "1.0");
}
