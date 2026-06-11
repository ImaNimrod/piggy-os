#ifndef _KERNEL_CPU_SMP_H
#define _KERNEL_CPU_SMP_H

#include <cpu/gdt.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/macros.h>

struct cpu_local {
    struct cpu_local* self;         // do not move 

    uintptr_t scheduler_stack;      // do not move
    uintptr_t scratch;              // do not move

    struct scheduler scheduler;     // do not move

    struct gdt gdt;
    struct tss tss;

    size_t fpu_context_size;
    void (*fpu_save)(void*);
    void (*fpu_restore)(void*);

    bool has_smap;

    struct timer_driver* timer_driver;
    struct timer_info* timer_info;
    uint64_t timer_base_ticks;
    uint64_t timer_tick_offset;

    size_t cpu_number;
    uint32_t lapic_id;
    uint32_t lapic_ticks_per_ms;
};

extern uintptr_t bsp_lapic_addr;
extern uint32_t bsp_lapic_id;
extern size_t cpu_count;
extern struct cpu_local* cpu_local_data;
extern bool use_x2apic;

void smp_halt_other_cpus(void);
void smp_init(void);

static ALWAYS_INLINE struct cpu_local* this_cpu(void) {
    struct cpu_local* this;
    asm volatile("mov %%gs:0, %0" : "=r" (this));
    return this;
}

#endif /* _KERNEL_CPU_SMP_H */
