#ifndef _KERNEL_DEV_IOAPIC_H
#define _KERNEL_DEV_IOAPIC_H

#include <stdbool.h>
#include <stdint.h>

bool ioapic_redirect_irq(uint8_t irq, uint8_t vector);
bool ioapic_set_irq_mask(uint8_t irq, bool mask);
void ioapic_init(uint8_t id, uintptr_t paddr, uint32_t gsi_base);

#endif /* _KERNEL_DEV_IOAPIC_H */
