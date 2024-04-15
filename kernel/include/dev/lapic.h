#ifndef _KERNEL_DEV_LAPIC_H
#define _KERNEL_DEV_LAPIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void lapic_eoi(void);
void lapic_timer_stop(void);
void lapic_init(uint8_t lapic_id);
void disable_pic(void);

#endif /* _KERNEL_DEV_LAPIC_H */
