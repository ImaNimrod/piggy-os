#ifndef _KERNEL_DEV_PIT_H
#define _KERNEL_DEV_PIT_H

#include <stdint.h>

void pit_play_sound(uint16_t hz);
void pit_stop_sound(void);

void pit_init(uint16_t hz);

#endif /* _KERNEL_DEV_PIT_H */
