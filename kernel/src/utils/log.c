#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <cpu/smp.h>
#include <dev/lapic.h>
#include <dev/serial.h>
#include <stdarg.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define PANIC_IPI 0xff

static spinlock_t print_lock = {0};

static void print_number(int64_t number, size_t radix, char padding, int pad_length, bool is_unsigned) {
    if (radix > 16) {
        return;
    }

    int arr[50];
    int count = 0;

    if (number < 0 && !is_unsigned) {
        outb(0xe9, '-');
        number = -number;
    }

    do {
        arr[count++] = number % radix;
        number /= radix;
    } while(number);

    int i;

    if (pad_length > count) {
        for (i = 0; i < pad_length - count; i++) {
            outb(0xe9, padding);
        }
    }

    for (i = count - 1; i > -1; i--) {
        outb(0xe9, (arr[i] < 10 ? '0' + arr[i] : 'a' + arr[i] - 10));
    }
}

static void puts(const char* str) {
    while (*str != '\0') {
        outb(0xe9, *str);
        str++;
    }
}

static void klog_internal(const char* fmt, va_list args) {
    const char* str;

    char c;
    while ((c = *(fmt++)) != '\0') {
        if (c != '%') {
            outb(0xe9, c);
        } else {
            char padding = ' ';
            int pad_to = 0;

            c = *(fmt++);

            if (c == '0') {
                padding = '0';
            }

            while (c >= '0' && c <= '9') {
                pad_to = pad_to * 10 + (c - '0');
                c = *(fmt++);
            }

            switch (c) {
                case '\0':
                    return;
                case '%':
                    outb(0xe9, '%');
                    break;
                case 'd':
                case 'u':
                    print_number(va_arg(args, int64_t), 10, padding, pad_to, c == 'd' ? false : true);
                    break;
                case 'x':
                case 'o':
                    print_number(va_arg(args, int64_t), c == 'x' ? 16 : 8, padding, pad_to, true);
                    break;
                case 'c':
                    outb(0xe9, (char) va_arg(args, int));
                    break;
                case 's':
                    str = va_arg(args, const char*);
                    if (!str) {
                        puts("(null)");
                        break;
                    }
                    puts(str);
                    break;
                default:
                    outb(0xe9, c);
                    break;
            }
        }
    }
}

static void print_stack_trace(uintptr_t* base_ptr) {
    if (base_ptr == NULL) {
        return;
    }

    puts("stack trace:\n");
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

void klog(const char* fmt, ...) {
    if (!cmdline_early_get_klog() || fmt == NULL) {
        return;
    }

    spinlock_acquire(&print_lock);

    va_list args;
    va_start(args, fmt);

    klog_internal(fmt, args);

    va_end(args);

    spinlock_release(&print_lock);
}

__attribute__((noreturn)) void kpanic(struct registers* r, bool trace_stack, const char* fmt, ...) {
    cli();

    if (cmdline_early_get_klog()) {
        puts("\n\n==================================| KERNEL PANIC |=============================================\nkernel panicked due to reason: ");

        va_list args;
        va_start(args, fmt);

        klog_internal(fmt, args);

        va_end(args);

        if (r != NULL) {
            puts("\n===============================================================================================\n");
            klog("RAX: 0x%016x RBX: 0x%016x RCX: 0x%016x RDX: 0x%016x\n", r->rax, r->rbx, r->rcx, r->rdx);
            klog("RSI: 0x%016x RDI: 0x%016x RSP: 0x%016x RBP: 0x%016x\n", r->rsi, r->rdi, r->rsp, r->rbp);
            klog("R8:  0x%016x R9:  0x%016x R10: 0x%016x R11: 0x%016x\n", r->r8, r->r9, r->r10, r->r11);
            klog("R12: 0x%016x R13: 0x%016x R14: 0x%016x R15: 0x%016x\n", r->r12, r->r13, r->r14, r->r15);
            klog("CR0: 0x%016x CR2: 0x%016x CR3: 0x%016x CR4: 0x%016x\n", read_cr0(), read_cr2(), read_cr3(), read_cr4());
            klog("RIP: 0x%016x RFLAGS: 0x%016x CS: 0x%04x SS: 0x%04x ERROR CODE: 0x%08x", r->rip, r->rflags, r->cs, r->ss, r->error_code);
        }

        if (trace_stack) {
            puts("\n===============================================================================================\nstack trace:\n");
            uintptr_t* base_ptr;
            __asm__ volatile("movq %%rbp,%0" : "=r" (base_ptr) ::);
            print_stack_trace(base_ptr);

        }

        puts("\n===============================================================================================\n");
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
