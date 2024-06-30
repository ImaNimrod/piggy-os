#ifndef _KERNEL_DEV_LAPIC_H
#define _KERNEL_DEV_LAPIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void lapic_eoi(void);
void lapic_send_ipi(uint32_t lapic_id, uint32_t vector);
void lapic_timer_oneshot(uint8_t vector, uint64_t us);
void lapic_timer_stop(void);
void lapic_init(void);

void disable_pic(void);

#endif /* _KERNEL_DEV_LAPIC_H */
