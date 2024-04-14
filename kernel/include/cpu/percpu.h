#ifndef _KERNEL_CPU_PERCPU_H
#define _KERNEL_CPU_PERCPU_H

#include <cpu/gdt.h>
#include <stddef.h>
#include <stdint.h>

struct percpu {
    size_t cpu_number;
    uintptr_t kernel_stack;
    uintptr_t user_stack;
	struct tss tss;
    uint32_t lapic_id;
};

#endif /* _KERNEL_CPU_PERCPU_H */
