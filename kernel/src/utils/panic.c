#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <dev/pit.h>
#include <utils/cmdline.h>
#include <utils/log.h> 

static void play_foghorn(void) {
    pit_play_sound(349);    // F4 @ 349.23Hz
    hpet_sleep_ms(600);
    pit_play_sound(245);    // B3 @ 246.94Hz
    hpet_sleep_ms(700);
    pit_stop_sound();
}

static void print_stack_trace(uintptr_t* base_ptr) {
    if (base_ptr == NULL) {
        return;
    }

    klog("stack trace:\n");
    for (;;) {
        uintptr_t* old_bp = (uintptr_t*) base_ptr[0];
        uintptr_t* ret_addr = (uintptr_t*) base_ptr[1];
        if (old_bp == NULL || ret_addr == NULL || (uintptr_t) ret_addr < HIGH_VMA) {
            break;
        }

        klog("      [0x%016x]\n", (uintptr_t) ret_addr);
        base_ptr = old_bp;
    }
}

__attribute__((noreturn)) void kpanic(struct registers* r, bool trace_stack, const char* fmt, ...) {
    cli();

    play_foghorn();

    if (cmdline_early_get_klog()) {
        klog("\n\n==================================| KERNEL PANIC |=============================================\nkernel panicked due to reason: ");

        va_list args;
        va_start(args, fmt);

        vklog(fmt, args);

        va_end(args);

        if (r != NULL) {
            klog("\n===============================================================================================\n");
            klog("RAX: 0x%016x RBX: 0x%016x RCX: 0x%016x RDX: 0x%016x\n", r->rax, r->rbx, r->rcx, r->rdx);
            klog("RSI: 0x%016x RDI: 0x%016x RSP: 0x%016x RBP: 0x%016x\n", r->rsi, r->rdi, r->rsp, r->rbp);
            klog("R8:  0x%016x R9:  0x%016x R10: 0x%016x R11: 0x%016x\n", r->r8, r->r9, r->r10, r->r11);
            klog("R12: 0x%016x R13: 0x%016x R14: 0x%016x R15: 0x%016x\n", r->r12, r->r13, r->r14, r->r15);
            klog("CR0: 0x%016x CR2: 0x%016x CR3: 0x%016x CR4: 0x%016x\n", read_cr0(), read_cr2(), read_cr3(), read_cr4());
            klog("RIP: 0x%016x RFLAGS: 0x%016x CS: 0x%04x SS: 0x%04x ERROR CODE: 0x%08x", r->rip, r->rflags, r->cs, r->ss, r->error_code);
        }

        if (trace_stack) {
            klog("\n===============================================================================================\n");
            uintptr_t* base_ptr;
            __asm__ volatile("movq %%rbp, %0" : "=r" (base_ptr) ::);
            print_stack_trace(base_ptr);

        }

        klog("===============================================================================================\n");
    }

    size_t cpu_number = this_cpu()->cpu_number;
    for (size_t i = 0; i < smp_cpu_count; i++) {
        if (i != cpu_number) {
            lapic_send_ipi(percpus[i].lapic_id, PANIC_IPI);
        }
    }

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}
