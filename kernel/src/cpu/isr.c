#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <dev/ioapic.h>
#include <sys/process.h> 
#include <utils/log.h>
#include <utils/math.h>

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

static isr_handler_t isr_handlers[ISR_HANDLER_NUM] = {0};

bool isr_install_handler(uint8_t vector, bool external_irq, isr_handler_t handler) {
    if (external_irq) {
        uint8_t irq_number = vector - ISR_IRQ_VECTOR_BASE;
        if (irq_number > ioapic_get_max_external_irqs()) {
            return false;
        }

        if (irq_number > 15) {
            ioapic_set_irq_vector(irq_number, vector);
        }
    }

    isr_handlers[vector] = handler;
    return true;
}

void isr_uninstall_handler(uint8_t vector) {
    isr_handlers[vector] = NULL;
}

void isr_handler(struct registers* r) {
	if (r->cs & 3) {
		swapgs();
    }

    uint8_t int_number = r->int_number & 0xff;
    if (isr_handlers[int_number] != NULL) {
        isr_handlers[int_number](r);
    } else if (int_number < ISR_EXCEPTION_NUM) {
        kpanic(r, "unhandled %s", exception_messages[int_number]);
        return;
    }

	if (r->cs & 3) {
		swapgs();
    }
} 
