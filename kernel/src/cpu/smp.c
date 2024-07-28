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

    wrmsr(IA32_GS_BASE_MSR, (uint64_t) percpu);

    percpu->tss.rsp0 = pmm_alloc(CPU_STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
    percpu->tss.ist1 = pmm_alloc(CPU_STACK_SIZE / PAGE_SIZE) + HIGH_VMA;

    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();
    uint64_t xcr0 = 0;

    uint32_t ebx = 0, ecx = 0, edx = 0, unused;

    if (cpuid(1, 0, &unused, &unused, &ecx, &edx)) {
        /* enable global pages if supported */
        if (edx & (1 << 13)) {
            cr4 |= (1 << 7);
        }

        /* enable XSAVE */
        if (ecx & (1 << 26)) {
            cr4 |= (1 << 18);
            xcr0 |= (1 << 0) | (1 << 1);

            /* if XSAVE is available, enable AVX */
            if (ecx & (1 << 28)) {
                xcr0 |= (1 << 2);
            }
        }
    }

    /* enable SSE instruction sets */
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);

    cr4 |= (1 << 9) | (1 << 10);

    if (cpuid(7, 0, &unused, &ebx, &unused, &unused)) {
        /* enable SMEP and SMAP if supported */
        if (ebx & (1 << 7)) {
            cr4 |= (1 << 20);
        }

        if (ebx & (1 << 20)) {
            cr4 |= (1 << 21);
            percpu->smap_enabled = true;
        }

        /* if XSAVE is available and AVX512 is supported, enable AVX512 */
        if (xcr0 != 0 && ebx & (1 << 16)) {
            xcr0 |= (1 << 5) | (1 << 6) | (1 << 7);
        }
    }

    cpuid(13, 0, &unused, &unused, &ecx, &unused);

    write_cr0(cr0);
    write_cr4(cr4);

    if (xcr0 != 0) {
        write_xcr0(xcr0);

        percpu->fpu_storage_size = ecx;
        percpu->fpu_save = xsave;
        percpu->fpu_restore = xrstor;
    } else {
        percpu->fpu_storage_size = 512;
        percpu->fpu_save = fxsave;
        percpu->fpu_restore = fxrstor;
    }

    /* enable SYSCALL/SYSRET instructions */
    uint64_t efer = rdmsr(IA32_EFER_MSR);
    efer |= (1 << 0);
    wrmsr(IA32_EFER_MSR, efer);

    wrmsr(IA32_STAR_MSR, 0x13000800000000);
    wrmsr(IA32_LSTAR_MSR, (uint64_t) syscall_entry);
    wrmsr(IA32_SFMASK_MSR, (uint64_t) 0x700);

    percpu->running_thread = NULL;

    lapic_init(percpu->lapic_id);

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
