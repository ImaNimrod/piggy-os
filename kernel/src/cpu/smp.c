#include <cpu/asm.h>
#include <cpu/idt.h>
#include <cpu/isr.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <sys/signal.h>
#include <utils/cmdline.h>
#include <utils/log.h>

extern struct limine_mp_request mp_request;

uintptr_t bsp_lapic_addr;
uint32_t bsp_lapic_id;
size_t cpu_count = 1;
struct cpu_local* cpu_local_data;
bool use_x2apic;

static size_t initialized_cpus;
static size_t synced_cpus;

static volatile uint64_t sync_sec;
static volatile uint64_t sync_usec;
static volatile bool sync_ready;

extern void syscall_entry(void);

static void fxsave(void* ctx) {
    asm volatile("fxsave (%0)" :: "r"(ctx) : "memory");
}

static void fxrstor(void* ctx) {
    asm volatile("fxrstor (%0)" :: "r"(ctx) : "memory");
}

static void xsave(void* ctx) {
    asm volatile("xsave (%0)" :: "r"(ctx), "a"(0xffffffff), "d"(0xffffffff) : "memory");
}

static void xsaveopt(void* ctx) {
    asm volatile("xsaveopt (%0)" :: "r"(ctx), "a"(0xffffffff), "d"(0xffffffff) : "memory");
}

static void xrstor(void* ctx) {
    asm volatile("xrstor (%0)" :: "r"(ctx), "a"(0xffffffff), "d"(0xffffffff) : "memory");
}

static void hang(struct limine_mp_info* mp_info) {
    (void) mp_info;
    cli();
    for (;;) {
        hlt();
    }
}

static void sigbus_handler(struct registers* r, void* arg) {
    (void) arg;

    if (r->cs == USER_CODE_SEGMENT) {
        signal_send_thread(this_cpu()->scheduler.current_thread, SIGBUS);
    } else {
        kpanic(r, true, "exception: %s", EXCEPTION_MESSAGES[r->int_number]);
    }
}

static void sigfpe_handler(struct registers* r, void* arg) {
    (void) arg;

    if (r->cs == USER_CODE_SEGMENT) {
        signal_send_thread(this_cpu()->scheduler.current_thread, SIGFPE);
    } else {
        kpanic(r, true, "exception: %s", EXCEPTION_MESSAGES[r->int_number]);
    }
}

static void sigill_handler(struct registers* r, void* arg) {
    (void) arg;

    if (r->cs == USER_CODE_SEGMENT) {
        signal_send_thread(this_cpu()->scheduler.current_thread, SIGILL);
    } else {
        kpanic(r, true, "exception: %s", EXCEPTION_MESSAGES[r->int_number]);
    }
}

static void sigsegv_handler(struct registers* r, void* arg) {
    (void) arg;

    if (r->cs == USER_CODE_SEGMENT) {
        signal_send_thread(this_cpu()->scheduler.current_thread, SIGSEGV);
    } else {
        kpanic(r, true, "exception: %s", EXCEPTION_MESSAGES[r->int_number]);
    }
}

static void sigtrap_handler(struct registers* r, void* arg) {
    (void) arg;

    if (r->cs == USER_CODE_SEGMENT) {
        signal_send_thread(this_cpu()->scheduler.current_thread, SIGTRAP);
    } else {
        kpanic(r, true, "exception: %s", EXCEPTION_MESSAGES[r->int_number]);
    }
}

static void single_cpu_init(struct limine_mp_info* mp_info) {
    struct cpu_local* cpu_local = (struct cpu_local*) mp_info->extra_argument;
    cpu_local->self = cpu_local;
    cpu_local->cpu_number = mp_info->processor_id;
    cpu_local->lapic_id = mp_info->lapic_id;

    wrmsr(MSR_IA32_GS_BASE, (uint64_t) cpu_local);

    gdt_reload();
    idt_reload();

    if (cpu_local->lapic_id != bsp_lapic_id) {
        pagemap_load(kernel_pagemap);
    }

    timer_early_percpu_init();

    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();

    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, unused;

    // Disable legacy FPU coprocessor things
    cr0 &= ~((1 << 2) | (1 << 3));
    cr0 |= (1 << 1);

    // Enable SSE instruction sets
    cr4 |= (1 << 9) | (1 << 10);

    cpuid(7, 0, &unused, &ebx, &ecx, &unused);

    // If SMEP is supported, enable it
    if (ebx & (1 << 7)) {
        cr4 |= (1 << 20);
    }

    // If SMAP is supported, enable it
    if (ebx & (1 << 20)) {
        cr4 |= (1 << 21);
        cpu_local->has_smap = true;
    }

    // If UMIP is supported, enable it
    if (ecx & (1 << 2)) {
        cr4 |= (1 << 11);
    }

    // If XSAVE/XRSTOR is supported, enable it
    bool has_xsave = false;
    cpuid(1, 0, &unused, &unused, &ecx, &unused);
    if (ecx & (1 << 26)) {
        has_xsave = true;
        cr4 |= (1 << 18);
    }

    write_cr0(cr0);
    write_cr4(cr4);

    uint64_t xcr0 = 0;

    if (has_xsave) {
        cpuid(13, 0, &eax, &unused, &unused, &edx);
        xcr0 = ((uint64_t) edx << 32) | eax;
    }

    if (xcr0 != 0) {
        write_xcr0(xcr0);
    }

    if (xcr0 != 0) {
        cpuid(13, 0, &unused, &ebx, &unused, &unused);
        cpu_local->fpu_context_size = ebx;

        cpuid(13, 1, &eax, &unused, &unused, &unused);
        cpu_local->fpu_save = (eax & (1 << 0)) ? xsaveopt : xsave;
        cpu_local->fpu_restore = xrstor;

        klog("[smp] CPU #%zu using xsave/xrstor (mask: 0x%lx size: %zu)\n",
                this_cpu()->cpu_number, xcr0, ebx);
    } else {
        cpu_local->fpu_context_size = 512;
        cpu_local->fpu_save = fxsave;
        cpu_local->fpu_restore = fxrstor;

        klog("[smp] CPU #%zu using legacy fxsave/fxrstor\n", this_cpu()->cpu_number);
    }

    // Enable SYSCALL/SYSRET instructions
    uint64_t efer = rdmsr(MSR_IA32_EFER);
    efer |= (1 << 0);
    wrmsr(MSR_IA32_EFER, efer);

    wrmsr(MSR_IA32_STAR, 0x13000800000000);
    wrmsr(MSR_IA32_LSTAR, (uint64_t) syscall_entry);
    wrmsr(MSR_IA32_SFMASK, (uint64_t) 0x700);

    // Use the same lapic base address mapping for all cpus
    if (cpu_local->lapic_id != bsp_lapic_id) {
        wrmsr(MSR_IA32_APIC_BASE, bsp_lapic_addr | (rdmsr(MSR_IA32_APIC_BASE) & 0xfff));
    }

    lapic_percpu_init();

    scheduler_percpu_init();
    timer_percpu_init();

    klog("[smp] CPU #%zu online%s\n", cpu_local->cpu_number, (cpu_local->lapic_id == bsp_lapic_id ? " (BSP)" : ""));
    __atomic_add_fetch(&initialized_cpus, 1, __ATOMIC_SEQ_CST);

    if (cpu_local->lapic_id != bsp_lapic_id) {
        uint64_t hz = cpu_local->timer_info->hz;
        uint64_t mhz = hz / 1000000;

        while (!sync_ready) {}

        cpu_local->timer_base_ticks = cpu_local->timer_driver->ticks(cpu_local->timer_info);
        cpu_local->timer_tick_offset = (sync_sec * hz) + (sync_usec * mhz);

        __atomic_add_fetch(&synced_cpus, 1, __ATOMIC_SEQ_CST);

        scheduler_await();
    }
}

void smp_halt_other_cpus(void) {
    if (cpu_count > 1) {
        lapic_send_ipi(LAPIC_IPI_ALL_OTHER_CPUS, PANIC_IPI_VECTOR);
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

            isr_register_handler(17, sigbus_handler, NULL);

            isr_register_handler(0, sigfpe_handler, NULL);
            isr_register_handler(16, sigfpe_handler, NULL);
            isr_register_handler(19, sigfpe_handler, NULL);

            isr_register_handler(6, sigill_handler, NULL);

            isr_register_handler(10, sigsegv_handler, NULL);
            isr_register_handler(11, sigsegv_handler, NULL);
            isr_register_handler(12, sigsegv_handler, NULL);
            isr_register_handler(13, sigsegv_handler, NULL);

            isr_register_handler(1, sigtrap_handler, NULL);
            isr_register_handler(3, sigtrap_handler, NULL);

            if (!use_x2apic) {
                bsp_lapic_addr = rdmsr(MSR_IA32_APIC_BASE) & ~(0xffful);
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
        struct timer_driver* timer_driver = this_cpu()->timer_driver;
        struct timer_info* timer_info = this_cpu()->timer_info;
        uint64_t hz = timer_info->hz;
        uint64_t mhz = hz / 1000000;

        while (__atomic_load_n(&initialized_cpus, __ATOMIC_SEQ_CST) != mp_response->cpu_count)  {
            pause();
        }

        uint64_t ticks = timer_driver->ticks(timer_info) - this_cpu()->timer_base_ticks;
        sync_sec = ticks / hz;
        sync_usec = (ticks % hz) / mhz;
        sync_ready = true;

        while (__atomic_load_n(&synced_cpus, __ATOMIC_SEQ_CST) != mp_response->cpu_count - 1)  {
            pause();
        }
    }

    cpu_count = initialized_cpus;
    klog("[smp] initialized %zu processor%c\n", initialized_cpus, (initialized_cpus == 1 ? '\0' : 's'));
}
