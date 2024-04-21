#ifndef _KERNEL_DEV_HPET
#define _KERNEL_DEV_HPET

#include <stdint.h>

#define HPET_CALC_SLEEP_NS(ns) (hpet_count() + ((ns) * 1000000) / hpet_clock_period)
#define HPET_CALC_SLEEP_MS(ms) (HPET_CALC_SLEEP_NS((ms) * 1000 * 1000))

extern uint32_t hpet_clock_period;

uint64_t hpet_count(void);
void hpet_sleep_ms(uint64_t ms);
void hpet_sleep_ns(uint64_t ns);
void hpet_init(void);

#endif /* _KERNEL_DEV_HPET */
