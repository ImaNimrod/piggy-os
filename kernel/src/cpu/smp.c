#include <cpuid.h>
#include <cpu/asm.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <limine.h>
#include <mem/heap.h>
#include <mem/vmm.h>
#include <utils/log.h>

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

static uint32_t bsp_lapic_id = 0;
static size_t cpu_count = 0;
static size_t initialized_cpus = 0;

static void single_cpu_init(struct limine_smp_info* smp_info) {
    gdt_reload();
    idt_reload();

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    struct percpu* percpu = (struct percpu*) smp_info->extra_argument;
    wrmsr(MSR_KERNEL_GS, (uint64_t) percpu);
    wrmsr(MSR_USER_GS, (uint64_t) percpu);

    uint32_t ecx = 0, edx = 0, unused;
    __get_cpuid(7, &unused, &unused, &ecx, &unused);
    __get_cpuid(1, &unused, &unused, &unused, &edx);

    /* enable SSE/SSE2 instructions */
    uint64_t cr0 = read_cr0();
    cr0 |= (1 << 1);
    cr0 &= ~(1 << 2);
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

    // TODO: enable SYSCALL/SYSRET instructions
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= (1 << 0);

    /* enable hardware FXSR instructions if supported */
    if (edx & (1 << 24)) {
        efer |= (1 << 14);
    }

    /* enable NX paging bit if supported */
    __get_cpuid(0x80000007, &unused, &unused, &unused, &edx);
    if (edx & (1 << 20)) {
        efer |= (1 << 14);
    }

    wrmsr(MSR_EFER, efer);
    
    klog("[smp] processor #%lu online\n", percpu->cpu_number);
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (percpu->lapic_id != bsp_lapic_id) {
        sti();
        for (;;) {
            hlt();
        }
    }
}

void smp_init(void) {
    struct limine_smp_response* smp_response = smp_request.response;
    bsp_lapic_id = smp_response->bsp_lapic_id;
    cpu_count = smp_response->cpu_count;

    klog("[smp] %lu processors detected\n", cpu_count);

    struct percpu* percpus = kcalloc(cpu_count, sizeof(struct percpu));

    for (size_t i = 0; i < cpu_count; i++) {
        struct limine_smp_info* cpu = smp_response->cpus[i];

		cpu->extra_argument = (uint64_t) &percpus[i];
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

    klog("[smp] initialized %lu cpus\n", initialized_cpus);
}
