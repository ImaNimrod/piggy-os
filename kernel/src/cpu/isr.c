#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/acpi/madt.h>
#include <dev/ioapic.h>
#include <dev/lapic.h>
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

static isr_handler_t isr_handlers[ISR_HANDLER_NUM] = {0};

bool isr_install_exception_handler(uint8_t exception_number, isr_handler_t handler) {
    if (exception_number >= ISR_EXCEPTION_NUM) {
        return false;
    } 

    isr_handlers[exception_number] = handler;
    return true;
}

bool isr_install_external_irq_handler(uint8_t irq_number, isr_handler_t handler) {
    if (irq_number > ioapic_get_max_external_irqs()) {
        return false;
    } 

    uint8_t vector = irq_number + ISR_IRQ_VECTOR_BASE;

    if (irq_number > 15) {
        ioapic_set_irq_vector(irq_number, vector);
    }

    isr_handlers[vector] = handler;
    return true;
}

void isr_uninstall_handler(uint8_t vector) {
    isr_handlers[vector] = NULL;
}

void isr_handler(struct registers* r) {
    if (r->cs & 0x03) {
        swapgs();
    }

    lapic_eoi();

    uint8_t int_number = r->int_number & 0xff;
    if (isr_handlers[int_number] != NULL) {
        isr_handlers[int_number](r);
        return;
    }

    if (int_number < ISR_EXCEPTION_NUM) {
        kpanic(r, "Unhandled Exception: %s", exception_messages[int_number]);
    }

    if (r->cs & 0x03) {
        swapgs();
    }
} 
