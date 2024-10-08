#ifndef _KERNEL_DEV_ACPI_ACPI_H
#define _KERNEL_DEV_ACPI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAS_ADDRESS_SPACE_MEMORY    0
#define GAS_ADDRESS_SPACE_IO        1
#define GAS_ADDRESS_SPACE_PCI       2

struct acpi_sdt {
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

struct acpi_gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t base;
} __attribute__((packed));

extern bool use_acpi_rev2;

struct acpi_sdt* acpi_find_sdt(const char signature[static 4]);
void acpi_init(void);
__attribute__((noreturn)) void acpi_reboot(void);
__attribute__((noreturn)) void acpi_shutdown(void);

#endif /* _KERNEL_ACPI_ACPI_H */
