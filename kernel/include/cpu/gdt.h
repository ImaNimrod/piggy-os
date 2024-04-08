#ifndef _KENREL_CPU_GDT_H
#define _KENREL_CPU_GDT_H

#include <stdint.h>

void gdt_reload(void);
void gdt_init(void);

#endif /* _KENREL_CPU_GDT_H */
