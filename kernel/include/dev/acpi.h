#ifndef _KERNEL_DEV_ACPI_H
#define _KERNEL_DEV_ACPI_H

int acpi_reboot(void);
int acpi_shutdown(void);

void acpi_early_init(void);
void acpi_init(void);

#endif /* _KERNEL_DEV_ACPI_H */
