#ifndef _KERNEL_DEV_PIT_H
#define _KERNEL_DEV_PIT_H

#include <stdint.h>

void pit_sound_off(void);
void pit_sound_on(uint16_t hz);

#endif /* _KERNEL_DEV_PIT_H */
