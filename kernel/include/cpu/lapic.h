#ifndef _KERNEL_CPU_LAPIC_H
#define _KERNEL_CPU_LAPIC_H

#include <stdint.h>

#define LAPIC_IPI_SELF              (uint32_t) ~1
#define LAPIC_IPI_ALL_CPUS          (uint32_t) ~2
#define LAPIC_IPI_ALL_OTHER_CPUS    (uint32_t) ~3

void lapic_eoi(void);
void lapic_send_ipi(uint32_t lapic_id, uint8_t vector);
void lapic_timer_periodic(uint8_t vector, uint64_t ms);
void lapic_timer_oneshot(uint8_t vector, uint64_t ms);
uint32_t lapic_timer_stop(void);
void lapic_percpu_init(void);

#endif /* _KERNEL_CPU_LAPIC_H */
