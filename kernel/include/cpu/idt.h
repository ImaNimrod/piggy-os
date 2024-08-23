#ifndef _KERNEL_CPU_IDT_H
#define _KERNEL_CPU_IDT_H

#include <stddef.h>
#include <stdint.h>

void idt_reload(void);
void idt_set_gate(uint8_t vector, uintptr_t handler, uint8_t ist);
void idt_set_ist(uint8_t vector, uint8_t ist);
void idt_init(void);

#endif /* _KERNEL_CPU_IDT_H */
