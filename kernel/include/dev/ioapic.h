#ifndef _KERNEL_DEV_IOAPIC_H
#define _KERNEL_DEV_IOAPIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ioapic_redirect_irq(uint8_t irq, uint8_t vector);
void ioapic_set_irq_mask(uint8_t irq, bool mask);
void register_ioapic(uint8_t id, uintptr_t paddr, uint32_t gsi_base);

#endif /* _KERNEL_DEV_IOAPIC_H */
