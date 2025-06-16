#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/lapic.h>
#include <mem/paging.h>
#include <stdarg.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

#include "../../src/utils/printf/printf.h"

static spinlock_t panic_lock = {0};

static void print_stack_trace(uintptr_t* base_ptr) {
    if (base_ptr == NULL) {
        return;
    }

    printf("stack trace:\n");
    for (;;) {
        uintptr_t* old_bp = (uintptr_t*) base_ptr[0];
        uintptr_t* ret_addr = (uintptr_t*) base_ptr[1];
        if (old_bp == NULL || ret_addr == NULL || (uintptr_t) ret_addr < HIGH_VMA) {
            break;
        }

        printf("      [0x%016lx]\n", (uintptr_t) ret_addr);
        base_ptr = old_bp;
    }
}

NORETURN void kpanic(struct registers* r, bool stack_trace, const char* fmt, ...) {
    cli();
    spinlock_acquire(&panic_lock);

    if (cpu_count > 1) {
        lapic_send_ipi(LAPIC_IPI_ALL_OTHER_CPUS, PANIC_IPI_VECTOR);
    }

    printf("\n\n==================================| KERNEL PANIC |=============================================\nCPU #%zu panicked due to reason: ", (cpu_count > 1 ? this_cpu()->cpu_number : 0));

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (r != NULL) {
        printf("\n===============================================================================================\n");
        printf("RAX: 0x%016lx RBX: 0x%016lx RCX: 0x%016lx RDX: 0x%016lx\n", r->rax, r->rbx, r->rcx, r->rdx);
        printf("RSI: 0x%016lx RDI: 0x%016lx RSP: 0x%016lx RBP: 0x%016lx\n", r->rsi, r->rdi, r->rsp, r->rbp);
        printf("R8:  0x%016lx R9:  0x%016lx R10: 0x%016lx R11: 0x%016lx\n", r->r8, r->r9, r->r10, r->r11);
        printf("R12: 0x%016lx R13: 0x%016lx R14: 0x%016lx R15: 0x%016lx\n", r->r12, r->r13, r->r14, r->r15);
        printf("CR0: 0x%016lx CR2: 0x%016lx CR3: 0x%016lx CR4: 0x%016lx\n", read_cr0(), read_cr2(), read_cr3(), read_cr4());
        printf("RIP: 0x%016lx RFLAGS: 0x%016lx CS: 0x%04x SS: 0x%04x ERROR CODE: 0x%08x", r->rip, r->rflags, r->cs, r->ss, r->error_code);
    }

    if (stack_trace) {
        printf("\n===============================================================================================");
        uintptr_t* base_ptr;
        asm volatile("movq %%rbp, %0" : "=r" (base_ptr) ::);
        print_stack_trace(base_ptr);
    }

    printf("\n===============================================================================================\n");

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}
