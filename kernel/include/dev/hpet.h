#ifndef _KERNEL_DEV_HPET_H
#define _KERNEL_DEV_HPET_H 1

#include <stdbool.h>
#include <stdint.h>

#define MS_TO_NS(ms) ((ms) * 1000000)
#define US_TO_NS(us) ((us) * 1000)

void hpet_sleep_ns(uint64_t ns);
void hpet_init(void);

#endif /* _KERNEL_DEV_HPET_H */
