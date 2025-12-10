#ifndef _KERNEL_CPU_IDT_H
#define _KERNEL_CPU_IDT_H

#include <stdint.h>

void idt_reload(void);
void idt_set_ist(uint8_t vector, uint8_t ist);
void idt_init(void); // this function should only be run once by BSP

#endif /* _KERNEL_CPU_IDT_H */
