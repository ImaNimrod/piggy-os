#ifndef _KERNEL_DEV_ACPI_ACPI_H
#define _KERNEL_DEV_ACPI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct sdt {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct sdt* acpi_find_sdt(const char signature[static 4]);
void acpi_init(void);

#endif /* _KERNEL_ACPI_ACPI_H */
