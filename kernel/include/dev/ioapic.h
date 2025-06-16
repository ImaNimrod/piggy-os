#ifndef _KERNEL_DEV_IOAPIC_H
#define _KERNEL_DEV_IOAPIC_H 1

#include <stdbool.h>
#include <stdint.h>

#define IOAPIC_POLARITY_ACTIVE_HIGH 0
#define IOAPIC_POLARITY_ACTIVE_LOW  1

#define IOAPIC_TRIGGER_EDGE     0
#define IOAPIC_TRIGGER_LEVEL    1

bool ioapic_redirect_irq(uint8_t irq, uint8_t vector);
bool ioapic_set_irq_mask(uint8_t irq, bool mask);
void ioapic_set_isa_iso(uint8_t irq, uint32_t gsi, int polarity, int trigger_mode);
void ioapic_init(uint8_t id, uintptr_t paddr, uint32_t gsi_base);

#endif /* _KERNEL_DEV_IOAPIC_H */
