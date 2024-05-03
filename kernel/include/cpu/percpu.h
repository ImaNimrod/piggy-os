#ifndef _KERNEL_CPU_PERCPU_H
#define _KERNEL_CPU_PERCPU_H

#include <cpu/asm.h>
#include <cpu/gdt.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>

struct percpu {
    struct percpu* self;
    size_t cpu_number;
    uintptr_t kernel_stack;
    uintptr_t user_stack;
    struct thread* running_thread;
	struct tss tss;
    size_t fpu_storage_size;
    void (*fpu_save)(void*);
    void (*fpu_restore)(void*);
    uint32_t lapic_id;
    uint32_t lapic_frequency;
};

static inline struct percpu* this_cpu(void) {
    struct percpu* ret;
    __asm__ volatile ("mov %%gs:0x0, %0" : "=r" (ret));
    return ret;
}

#endif /* _KERNEL_CPU_PERCPU_H */
