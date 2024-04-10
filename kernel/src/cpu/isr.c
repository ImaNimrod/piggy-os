#include <cpu/asm.h>
#include <cpu/isr.h>
#include <utils/log.h>
#include <utils/math.h>

static const char* exception_messages[] = {
    "Divide by zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double fault",
    "Co-processor Segment Overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-Segment Fault",
    "GPF",
    "Page Fault",
    "Reserved",
    "x87 Floating Point Exception",
    "alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization Exception",
    "Deadlock",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved",
    "Triple Fault",
    "FPU error",
};

static isr_handler_t isr_handlers[256] = {0};

uint8_t isr_alloc_vector(isr_handler_t handler) {
    for (size_t i = 0; i < SIZEOF_ARRAY(isr_handlers); i++) {
        if (!isr_handlers[i]) {
            isr_handlers[i] = handler;
            return i;
        }
    }

    return (uint8_t) -1;
}

void isr_free_vector(uint8_t vector) {
    isr_handlers[vector] = NULL;
}

void isr_handler(struct registers* r) {
    if (r->cs & 0x03) {
        swapgs();
    }

    uint8_t int_number = r->int_number & 0xff;
    if (int_number < 32) {
        kpanic(r, "EXCEPTION TRIGGERED: %s", exception_messages[int_number]);

        cli();
        for (;;) {
            hlt();
        }
    }

    if (r->cs & 0x03) {
        swapgs();
    }
} 
