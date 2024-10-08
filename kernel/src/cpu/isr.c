#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <sys/sched.h> 
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/log.h>

static const char* exception_messages[ISR_EXCEPTION_NUM] = {
    "Divide by Zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Co-processor Segment Overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating Point Exception",
    "Alignment Check Exception",
    "Machine Check Exception",
    "SIMD floating-point exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved",
};

struct isr_table_entry {
    isr_handler_t handler;
    void* ctx;
};

static struct isr_table_entry isr_table[ISR_NUM] = {0};

void isr_install_handler(uint8_t vector, isr_handler_t handler, void* ctx) {
    isr_table[vector] = (struct isr_table_entry) { .handler = handler, .ctx = ctx };
}

void isr_uninstall_handler(uint8_t vector) {
    isr_table[vector] = (struct isr_table_entry) {0};
}

void isr_handler(struct registers* r) {
    if (r->cs & 3) {
        swapgs();
    }

    uint8_t int_number = r->int_number & 0xff;

    struct isr_table_entry entry = isr_table[int_number];
    if (likely(entry.handler != NULL)) {
        entry.handler(r, entry.ctx);
    } else if (int_number < ISR_EXCEPTION_NUM) {
        if (r->cs & 3) {
            sched_thread_dequeue(this_cpu()->running_thread);
            sched_yield();
        } else {
            kpanic(r, true, "unhandled %s", exception_messages[int_number]);
        }
    }

    if (r->cs & 3) {
        swapgs();
    }
} 
