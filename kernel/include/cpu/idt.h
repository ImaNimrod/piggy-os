#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H

#include <stdint.h>

void idt_reload(void);
void idt_init(void);

#endif /* _KERNEL_IDT_H */
