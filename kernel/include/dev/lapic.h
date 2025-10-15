#ifndef _KERNEL_DEV_LAPIC_H
#define _KERNEL_DEV_LAPIC_H 1

#include <stdint.h>

#define LAPIC_IPI_SELF              (uint32_t) ~1
#define LAPIC_IPI_ALL_CPUS          (uint32_t) ~2
#define LAPIC_IPI_ALL_OTHER_CPUS    (uint32_t) ~3

void lapic_eoi(void);
void lapic_send_ipi(uint32_t lapic_id, uint8_t vector);
void lapic_timer_oneshot(uint8_t vector, uint64_t ms);
void lapic_timer_stop(void);
void lapic_init(void); // this function should be run once by each CPU

void legacy_pic_disable(void);

#endif /* _KERNEL_DEV_LAPIC_H */
