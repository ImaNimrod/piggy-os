#ifndef _KERNEL_CPU_SMP_H
#define _KERNEL_CPU_SMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PANIC_IPI 0xff

extern size_t smp_cpu_count;

void smp_init(void);

#endif /* _KERNEL_CPU_SMP_H */
