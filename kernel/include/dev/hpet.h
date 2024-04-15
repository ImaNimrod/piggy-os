#ifndef _KERNEL_DEV_HPET
#define _KERNEL_DEV_HPET

#include <stdint.h>

void hpet_sleep_ms(uint64_t ms);
void hpet_sleep_ns(uint64_t ns);
void hpet_init(void);

#endif /* _KERNEL_DEV_HPET */
