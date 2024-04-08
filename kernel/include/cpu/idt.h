#ifndef _KERNEL_CPU_IDT_H
#define _KERNEL_CPU_IDT_H

#include <stdint.h>

void idt_reload(void);
void idt_init(void);

#endif /* _KERNEL_CPU_IDT_H */
