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
#include <utils/cmdline.h>
#include <utils/log.h>

#define CPU_STACK_SIZE 0x8000

struct percpu* percpus = NULL;
size_t smp_cpu_count = 0;

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

static uint32_t bsp_lapic_id = 0;
static size_t initialized_cpus = 0;

extern void syscall_entry(void);

static void hang(struct limine_smp_info* smp_info) {
    (void) smp_info;
    cli();
    for (;;) {
        hlt();
    }
}

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

    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();
    uint32_t ebx = 0, ecx = 0, edx = 0, unused;

    if (__get_cpuid(1, &unused, &unused, &unused, &edx)) {
        /* enable global pages if supported */
        if (edx & (1 << 13)) {
            cr4 |= (1 << 7);
        }
    }

    /* enable SSE instruction sets */
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);

    cr4 |= (1 << 9) | (1 << 10);

    if (__get_cpuid(7, &unused, &ebx, &ecx, &unused)) {
        /* enable usermode instruction prevention if supported */ 
        if (ecx & (1 << 2)) {
            cr4 |= (1 << 11);
        }

        /* enable SMEP and SMAP if supported */
        if (ebx & (1 << 7)) {
            cr4 |= (1 << 20);
            percpu->smepsmap_enabled = true;
        }

        if (ebx & (1 << 20)) {
            cr4 |= (1 << 21);
            percpu->smepsmap_enabled = true;
        }
    }

    write_cr0(cr0);
    write_cr4(cr4);

    /* enable SYSCALL/SYSRET instructions */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= (1 << 0);
    wrmsr(MSR_EFER, efer);

    wrmsr(MSR_STAR, 0x13000800000000);
    wrmsr(MSR_LSTAR, (uint64_t) syscall_entry);
    wrmsr(MSR_SFMASK, (uint64_t) 0x700);

    percpu->fpu_storage_size = 512;
    percpu->fpu_save = fxsave;
    percpu->fpu_restore = fxrstor;

    percpu->running_thread = NULL;

    lapic_init();

    klog("[smp] processor #%u online%s\n", percpu->cpu_number, (percpu->lapic_id == bsp_lapic_id ? " (BSP)" : ""));
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (percpu->lapic_id != bsp_lapic_id) {
        sched_await();
    }
}

void smp_init(void) {
    struct limine_smp_response* smp_response = smp_request.response;
    smp_cpu_count = smp_response->cpu_count;
    bsp_lapic_id = smp_response->bsp_lapic_id;

    klog("[smp] %u processor%c detected\n", smp_cpu_count, (smp_cpu_count == 1 ? '\0' : 's'));

    bool nosmp = cmdline_get("nosmp") != NULL;
    if (nosmp) {
        klog("[smp] nosmp argument detected, only initializing BSP\n");
    }

    void (*cpu_goto_fn)(struct limine_smp_info*) = nosmp ? hang : single_cpu_init;

    percpus = kmalloc(sizeof(struct percpu) * smp_cpu_count);

    for (size_t i = 0; i < smp_cpu_count; i++) {
        struct limine_smp_info* cpu = smp_response->cpus[i];

        cpu->extra_argument = (uint64_t) &percpus[i];

        percpus[i].self = &percpus[i];
        percpus[i].cpu_number = i;
        percpus[i].lapic_id = i;

        if (cpu->lapic_id != bsp_lapic_id) {
            __atomic_store_n(&cpu->goto_address, cpu_goto_fn, __ATOMIC_SEQ_CST);
        } else {
            idt_init();
            single_cpu_init(cpu);
        }
    }

    if (!nosmp) {
        while (__atomic_load_n(&initialized_cpus, __ATOMIC_SEQ_CST) != smp_cpu_count)  {
            pause();
        }
    }

    klog("[smp] initialized %u cpu%c\n", initialized_cpus, (initialized_cpus == 1 ? '\0' : 's'));
}
