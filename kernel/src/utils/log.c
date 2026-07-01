#include <cpu/asm.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <dev/char/fb.h>
#include <dev/pit.h>
#include <dev/serial.h>
#include <flanterm.h>
#include <mem/paging.h>
#include <printf.h>
#include <stdarg.h>
#include <sys/timer.h>
#include <utils/log.h>
#include <utils/spinlock.h>

static spinlock_t panic_lock;
static spinlock_t print_lock;

static void print_stack_trace(uintptr_t* rbp) {
    if (rbp == NULL || ((uintptr_t) rbp) < HIGH_VMA) {
        return;
    }

    printf("stack trace:");

    for (;;) {
        uintptr_t* old_rbp = (uintptr_t*) rbp[0];
        uintptr_t* rip = (uintptr_t*) rbp[1];

        if (!rip || !old_rbp || ((uintptr_t) rip) < HIGH_VMA) {
            break;
        }

        printf("\n    - 0x%016lx", rip);

        rbp = old_rbp;
    }
}

void _putchar(char c) {
    if (c == '\n') {
        static const char crnl[2] = { '\r', '\n' };

        serial_putc(COM1_PORT, crnl[0]);
        serial_putc(COM1_PORT, crnl[1]);

        if (likely(fb_context)) {
            flanterm_write(fb_context, crnl, sizeof(crnl));
        }
    } else {
        serial_putc(COM1_PORT, c);

        if (likely(fb_context)) {
            flanterm_write(fb_context, &c, sizeof(char));
        }
    }
}

void klog(const char* fmt, ...) {
    bool int_state = spinlock_acquire_irqsave(&print_lock);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    spinlock_release_irqsave(&print_lock, int_state);
}

[[noreturn]] void kpanic(struct registers* r, bool stack_trace, const char* fmt, ...) {
    cli();
    spinlock_acquire(&panic_lock);

    spinlock_release(&print_lock);
    spinlock_acquire(&print_lock);

    smp_halt_other_cpus();

    printf("\n\n==================================| KERNEL PANIC |=============================================\nCPU #%zu panicked due to reason: ", (cpu_count > 1 ? this_cpu()->cpu_number : 0));

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (r) {
        printf("\n===============================================================================================\n");
        printf("RAX: 0x%016lx RBX: 0x%016lx RCX: 0x%016lx RDX: 0x%016lx\n", r->rax, r->rbx, r->rcx, r->rdx);
        printf("RSI: 0x%016lx RDI: 0x%016lx RSP: 0x%016lx RBP: 0x%016lx\n", r->rsi, r->rdi, r->rsp, r->rbp);
        printf("R8:  0x%016lx R9:  0x%016lx R10: 0x%016lx R11: 0x%016lx\n", r->r8, r->r9, r->r10, r->r11);
        printf("R12: 0x%016lx R13: 0x%016lx R14: 0x%016lx R15: 0x%016lx\n", r->r12, r->r13, r->r14, r->r15);
        printf("CR0: 0x%016lx CR2: 0x%016lx CR3: 0x%016lx CR4: 0x%016lx\n", read_cr0(), read_cr2(), read_cr3(), read_cr4());
        printf("RIP: 0x%016lx RFLAGS: 0x%016lx CS: 0x%04x SS: 0x%04x ERROR CODE: 0x%08x", r->rip, r->rflags, r->cs, r->ss, r->error_code);
    }

    if (stack_trace) {
        uintptr_t* rbp;
        if (r) {
            rbp = (uintptr_t*) r->rbp;
        } else {
            asm volatile("mov %%rbp, %0" : "=g" (rbp) :: "memory");
        }

        printf("\n===============================================================================================\n");
        print_stack_trace(rbp);
    }

    printf("\n===============================================================================================");

    pit_sound_on(330);
    timer_wait_ns(MS_TO_NS(900));
    pit_sound_on(233);
    timer_wait_ns(MS_TO_NS(900));
    pit_sound_off();

    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}
