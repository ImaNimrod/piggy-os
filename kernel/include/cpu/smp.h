#ifndef _KERNEL_CPU_SMP_H
#define _KERNEL_CPU_SMP_H 1

#include <cpu/gdt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>
#include <utils/macros.h>

struct cpu_local {
    struct cpu_local* self;

    struct thread* running_thread;
    struct thread* idle_thread;

    struct tss tss;

    size_t fpu_context_size;
    void (*fpu_save)(void*);
    void (*fpu_restore)(void*);

    uint64_t (*read_fs_base)(void);
    void (*write_fs_base)(uint64_t);
    uint64_t (*read_gs_base)(void);
    void (*write_gs_base)(uint64_t);

    bool has_smap;

    size_t cpu_number;
    uint32_t lapic_id;
    uint32_t lapic_frequency;
};

extern uintptr_t bsp_lapic_addr;
extern size_t cpu_count;
extern bool use_x2apic;

void smp_init(void);

static ALWAYS_INLINE struct cpu_local* this_cpu(void) {
    struct cpu_local* this;
    asm volatile("mov %%gs:0, %0" : "=r" (this));
    return this;
}

#endif /* _KERNEL_CPU_SMP_H */
