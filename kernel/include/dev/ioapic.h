#ifndef _KERNEL_DEV_IOAPIC_H
#define _KERNEL_DEV_IOAPIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t ioapic_get_max_external_irqs(void);
void ioapic_set_irq_mask(uint8_t irq, bool mask);
void ioapic_set_irq_vector(uint8_t irq, uint8_t vector);
void ioapic_set_isa_irq_routing(uint8_t isa_irq, uint8_t vector, uint16_t flags);
void register_ioapic(uint8_t id, uintptr_t address, uint32_t gsi_base);

#endif /* _KERNEL_DEV_IOAPIC_H */
