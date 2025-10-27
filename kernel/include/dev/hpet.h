#ifndef _KERNEL_DEV_HPET_H
#define _KERNEL_DEV_HPET_H

#include <stdbool.h>
#include <stdint.h>

void hpet_sleep_ns(uint64_t ns);
void hpet_init(uint16_t hz);

#endif /* _KERNEL_DEV_HPET_H */
