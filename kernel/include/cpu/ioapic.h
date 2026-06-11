#ifndef _KERNEL_CPU_IOAPIC_H
#define _KERNEL_CPU_IOAPIC_H

#include <stdint.h>

bool ioapic_redirect_irq(uint8_t irq, uint8_t vector);
bool ioapic_set_irq_mask(uint8_t irq, bool mask);
void ioapic_init(void);

#endif /* _KERNEL_CPU_IOAPIC_H */
