#include <cpu/asm.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <dev/lapic.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <sys/scheduler.h>
#include <utils/cmdline.h>
#include <utils/log.h>

extern struct limine_mp_request mp_request;

uintptr_t bsp_lapic_addr;
size_t cpu_count = 1;
struct cpu_local* cpu_local_data;
bool use_x2apic;

static uint32_t bsp_lapic_id;
static size_t initialized_cpus;

extern void syscall_entry(void);

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

    wrmsr(IA32_GS_BASE_MSR, (uint64_t) cpu_local);

    cpu_local->scheduler_stack = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB) + HIGH_VMA;
    cpu_local->tss.ist1 = cpu_local->scheduler_stack + KERNEL_STACK_SIZE;

    gdt_reload();
    idt_reload();

    if (cpu_local->lapic_id != bsp_lapic_id) {
        pagemap_load(kernel_pagemap);
    }

    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();

    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, unused;

    // disable nasty legacy coprocessor things
    cr0 &= ~((1 << 2) | (1 << 3));
    cr0 |= (1 << 1);

    // enable SSE instruction sets
    cr4 |= (1 << 9) | (1 << 10);

    cpuid(7, 0, &unused, &ebx, &ecx, &unused);

    // if FSGSBASE is supported, enable it
    if (ebx & (1 << 0)) {
        cr4 |= (1 << 16);
    }

    // if SMEP is supported, enable it
    if (ebx & (1 << 7)) {
        cr4 |= (1 << 20);
    }

    // if SMAP is supported, enable it
    if (ebx & (1 << 20)) {
        cr4 |= (1 << 21);
        cpu_local->has_smap = true;
    }

    // if UMIP is supported, enable it
    if (ecx & (1 << 2)) {
        cr4 |= (1 << 11);
    }

    // if XSAVE/XRSTOR is supported, enable it
    bool has_xsave = false;
    cpuid(1, 0, &unused, &unused, &ecx, &unused);
    if (ecx & (1 << 26)) {
        has_xsave = true;
        cr4 |= (1 << 18);
    }

    write_cr0(cr0);
    write_cr4(cr4);

    uint64_t xcr0 = 0;
    if (has_xsave && cpuid(13, 0, &eax, &unused, &unused, &edx)) {
        xcr0 = ((uint64_t) edx << 32) | eax;
    }

    if (xcr0 != 0) {
        write_xcr0(xcr0);
    }

    if (xcr0 != 0 && cpuid(13, 0, &unused, &ebx, &unused, &unused)) {
        cpu_local->fpu_context_size = ebx;

        cpuid(13, 1, &eax, &unused, &unused, &unused);
        cpu_local->fpu_save = (eax & (1 << 0)) ? xsaveopt : xsave;
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
    wrmsr(IA32_LSTAR_MSR, (uint64_t) syscall_entry);
    wrmsr(IA32_SFMASK_MSR, (uint64_t) 0x700);

    cpu_local->idle_thread = thread_create_kernel((uintptr_t) idle, NULL);
    cpu_local->running_thread = cpu_local->idle_thread;

    /* use the same lapic base address mapping for all cpus */ 
    if (cpu_local->lapic_id != bsp_lapic_id) {
        wrmsr(IA32_APIC_BASE_MSR, bsp_lapic_addr | (rdmsr(IA32_APIC_BASE_MSR) & 0xfff));
    }

    lapic_percpu_init();

    klog("[smp] processor #%zu online%s\n", cpu_local->cpu_number, (cpu_local->lapic_id == bsp_lapic_id ? " (BSP)" : ""));
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (cpu_local->lapic_id != bsp_lapic_id) {
        scheduler_await();
    }
}

void smp_init(void) {
    struct limine_mp_response* mp_response = mp_request.response;

    bsp_lapic_id = mp_response->bsp_lapic_id;
    use_x2apic = mp_response->flags & LIMINE_MP_REQUEST_X86_64_X2APIC;

    bool nosmp = cmdline_get("nosmp") != NULL;
    if (nosmp) {
        klog("[smp] 'nosmp' argument found, only initializing BSP\n");
    }

    void (*cpu_goto_fn)(struct limine_mp_info*) = nosmp ? hang : single_cpu_init;

    cpu_local_data = (struct cpu_local*) (pmm_alloc_zero(DIV_CEIL(sizeof(struct cpu_local) * mp_response->cpu_count, PAGE_SIZE_4KB)) + HIGH_VMA);

    for (size_t i = 0; i < mp_response->cpu_count; i++) {
        struct limine_mp_info* mp_info = mp_response->cpus[i];
        mp_info->extra_argument = (uint64_t) &cpu_local_data[i];

        if (mp_info->lapic_id == mp_response->bsp_lapic_id) {
            idt_init();
            idt_set_ist(SCHEDULER_IRQ_VECTOR, 1);

            if (!use_x2apic) {
                bsp_lapic_addr = rdmsr(IA32_APIC_BASE_MSR) & ~(0xffful);
                pagemap_map(kernel_pagemap, bsp_lapic_addr + HIGH_VMA, bsp_lapic_addr, PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NX, PAGE_SIZE_4KB);
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
