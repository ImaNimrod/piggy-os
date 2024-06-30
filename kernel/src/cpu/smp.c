#include <cpuid.h>
#include <cpu/asm.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/lapic.h>
#include <limine.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/sched.h>
#include <utils/log.h>

#define CPU_STACK_SIZE 0x8000

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

static uint32_t bsp_lapic_id = 0;
static size_t cpu_count = 0;
static size_t initialized_cpus = 0;

extern void syscall_entry(void);

static void single_cpu_init(struct limine_smp_info* smp_info) {
    struct percpu* percpu = (struct percpu*) smp_info->extra_argument;

    gdt_reload();
    idt_reload();

    gdt_load_tss(&percpu->tss);

    vmm_switch_pagemap(kernel_pagemap);

    wrmsr(MSR_KERNEL_GS, (uint64_t) percpu);
    wrmsr(MSR_USER_GS, (uint64_t) percpu);

    percpu->tss.rsp0 = pmm_alloc(CPU_STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
    percpu->tss.ist1 = pmm_alloc(CPU_STACK_SIZE / PAGE_SIZE) + HIGH_VMA;

    uint32_t ecx = 0, edx = 0, unused;
    __get_cpuid(7, &unused, &unused, &ecx, &unused);
    __get_cpuid(1, &unused, &unused, &ecx, &edx);

    /* enable SSE/SSE2 instructions */
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= (1 << 9) | (1 << 10);

    /* enable global pages if supported */
    if (edx & (1 << 13)) {
        cr4 |= (1 << 7);
    }
    /* enable usermode instruction prevention if supported */ 
    if (ecx & (1 << 2)) {
        cr4 |= (1 << 11);
    }

    write_cr4(cr4);

    /* enable SYSCALL/SYSRET instructions */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= (1 << 0);
    wrmsr(MSR_EFER, efer);

    wrmsr(MSR_STAR, 0x13000800000000);
    wrmsr(MSR_LSTAR, (uint64_t) syscall_entry);
    wrmsr(MSR_SFMASK, (uint64_t) ((1 << 9) | (1 << 10)));

    percpu->fpu_storage_size = 512;
    percpu->fpu_save = fxsave;
    percpu->fpu_restore = fxrstor;

    percpu->running_thread = NULL;

    lapic_init();

    klog("[smp] processor #%lu online%s\n", percpu->cpu_number, (percpu->lapic_id == bsp_lapic_id ? " (BSP)" : ""));
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (percpu->lapic_id != bsp_lapic_id) {
        sched_await();
    }
}

void smp_init(void) {
    struct limine_smp_response* smp_response = smp_request.response;
    bsp_lapic_id = smp_response->bsp_lapic_id;
    cpu_count = smp_response->cpu_count;

    klog("[smp] %lu processor%c detected\n", cpu_count, (cpu_count == 1 ? '\0' : 's'));

    struct percpu* percpus = kmalloc(sizeof(struct percpu) * cpu_count);

    for (size_t i = 0; i < cpu_count; i++) {
        struct limine_smp_info* cpu = smp_response->cpus[i];

        cpu->extra_argument = (uint64_t) &percpus[i];

        percpus[i].self = &percpus[i];
        percpus[i].cpu_number = i;
        percpus[i].lapic_id = i;

        if (cpu->lapic_id != bsp_lapic_id) {
            cpu->goto_address = single_cpu_init;
        } else {
            idt_init();
            single_cpu_init(cpu);
        }
    }

    while (initialized_cpus != smp_response->cpu_count) {
        pause();
    }

    klog("[smp] initialized %lu cpu%c\n", initialized_cpus, (initialized_cpus == 1 ? '\0' : 's'));
}
