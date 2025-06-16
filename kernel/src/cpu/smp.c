#include <cpu/asm.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <dev/acpi.h>
#include <dev/lapic.h>
#include <limine.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <sys/scheduler.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

extern struct limine_mp_request mp_request;

size_t cpu_count = 1;
bool use_x2apic = false;

static uint32_t bsp_lapic_id = 0;
static size_t initialized_cpus = 0;
static struct cpu_local* cpu_local_data = NULL;

static inline uint64_t read_fs_base_msr(void) {
    return rdmsr(IA32_FS_BASE_MSR);
}

static inline void write_fs_base_msr(uint64_t fs_base) {
    wrmsr(IA32_FS_BASE_MSR, fs_base);
}

static inline uint64_t read_gs_base_msr(void) {
    return rdmsr(IA32_GS_BASE_MSR);
}

static inline void write_gs_base_msr(uint64_t gs_base) {
    wrmsr(IA32_GS_BASE_MSR, gs_base);
}

static void idle(void) {
    for (;;) {
        hlt();
    }
}

static void hang(struct limine_mp_info* mp_info) {
    (void) mp_info;
    cli();
    for (;;) {
        hlt();
    }
}

static void single_cpu_init(struct limine_mp_info* mp_info) {
    struct cpu_local* cpu_local = (struct cpu_local*) mp_info->extra_argument;
    cpu_local->self = cpu_local;
    cpu_local->cpu_number = mp_info->processor_id;
    cpu_local->lapic_id = mp_info->lapic_id;

    gdt_init(&cpu_local->gdt);
    gdt_reload(&cpu_local->gdt);
    idt_reload();

    gdt_set_tss(&cpu_local->gdt, &cpu_local->tss);

    if (cpu_local->lapic_id != bsp_lapic_id) {
        vmm_switch_pagemap(kernel_pagemap);
    }

    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();
    uint64_t xcr0 = 0;

    uint32_t ebx = 0, ecx = 0, edx = 0, unused;

    if (cpuid(1, 0, &unused, &unused, &ecx, &edx)) {
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
        /* if XSAVE is available and AVX512 is supported, enable AVX512 */
        if (xcr0 != 0 && ebx & (1 << 16)) {
            xcr0 |= (1 << 5) | (1 << 6) | (1 << 7);
        }

        /* if FSGSBASE is supported, enable it */
        if (ebx & (1 << 0)) {
            cr4 |= (1 << 16);
            cpu_local->read_fs_base = rdfsbase;
            cpu_local->write_fs_base = wrfsbase;
            cpu_local->read_gs_base = rdgsbase;
            cpu_local->write_gs_base = wrgsbase;
        } else {
            cpu_local->read_fs_base = read_fs_base_msr;
            cpu_local->write_fs_base = write_fs_base_msr;
            cpu_local->read_gs_base = read_gs_base_msr;
            cpu_local->write_gs_base = write_gs_base_msr;
        }

        /* if SMEP is supported, enable it */
        if (ebx & (1 << 7)) {
            cr4 |= (1 << 20);
        }

        /* if SMAP is supported, enable it */
        if (ebx & (1 << 20)) {
            cr4 |= (1 << 21);
            cpu_local->has_smap = true;
        }
    }

    write_cr0(cr0);
    write_cr4(cr4);

    if (cpuid(13, 0, &unused, &unused, &ecx, &unused) && xcr0 != 0) {
        write_xcr0(xcr0);

        cpu_local->fpu_context_size = ecx;
        cpu_local->fpu_save = xsave;
        cpu_local->fpu_restore = xrstor;
    } else {
        cpu_local->fpu_context_size = 512;
        cpu_local->fpu_save = fxsave;
        cpu_local->fpu_restore = fxrstor;
    }

    /* enable SYSCALL/SYSRET instructions */
    uint64_t efer = rdmsr(IA32_EFER_MSR);
    efer |= (1 << 0);
    wrmsr(IA32_EFER_MSR, efer);

    wrmsr(IA32_STAR_MSR, 0x13000800000000);
    wrmsr(IA32_LSTAR_MSR, (uint64_t) NULL); // TODO: set syscall entry when syscalls are a thing
    wrmsr(IA32_SFMASK_MSR, (uint64_t) 0x700);

    cpu_local->idle_thread = kthread_create((uintptr_t) idle, NULL);
    cpu_local->running_thread = cpu_local->idle_thread;

    wrmsr(IA32_GS_BASE_MSR, (uint64_t) cpu_local);

    lapic_init();

    klog("[smp] processor #%zu online%s\n", cpu_local->cpu_number, (cpu_local->lapic_id == bsp_lapic_id ? " (BSP)" : ""));
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (cpu_local->lapic_id != bsp_lapic_id) {
        scheduler_await();
    }
}

void smp_init(void) {
    struct limine_mp_response* mp_response = mp_request.response;

    bsp_lapic_id = mp_response->bsp_lapic_id;
    use_x2apic = mp_response->flags & LIMINE_MP_X2APIC;

    bool nosmp = cmdline_get("nosmp") != NULL;
    if (nosmp) {
        klog("[smp] 'nosmp' argument detected, only initializing BSP\n");
    }

    void (*cpu_goto_fn)(struct limine_mp_info*) = nosmp ? hang : single_cpu_init;

    cpu_local_data = (struct cpu_local*) (pmm_alloc_zero(DIV_CEIL(sizeof(struct cpu_local) * mp_response->cpu_count, PAGE_SIZE)) + HIGH_VMA);

    for (size_t i = 0; i < mp_response->cpu_count; i++) {
        struct limine_mp_info* mp_info = mp_response->cpus[i];
        mp_info->extra_argument = (uint64_t) &cpu_local_data[i];

        if (mp_info->lapic_id == mp_response->bsp_lapic_id) {
            idt_init();

            if (!use_x2apic) {
                if (unlikely(!vmm_map_page(kernel_pagemap, madt_lapic_addr + HIGH_VMA, madt_lapic_addr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX))) {
                    kpanic(NULL, false, "failed to create page table mapping for LAPIC");
                }

                klog("[smp] processor is using XAPIC\n");
            } else {
                klog("[smp] processor is using X2APIC\n");
            }

            single_cpu_init(mp_info);
        } else {
            __atomic_store_n(&mp_info->goto_address, cpu_goto_fn, __ATOMIC_SEQ_CST);
        }
    }

    if (!nosmp) {
        while (__atomic_load_n(&initialized_cpus, __ATOMIC_SEQ_CST) != mp_response->cpu_count)  {
            pause();
        }
    }

    cpu_count = initialized_cpus;
    klog("[smp] initialized %zu processor%c\n", initialized_cpus, (initialized_cpus == 1 ? '\0' : 's'));
}
