#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <utils/log.h>
#include <utils/macros.h>

struct isr_table_entry {
    isr_handler_t handler;
    void* arg;
};

const char* EXCEPTION_MESSAGES[EXCEPTION_NUM] = {
    "Divide by Zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Co-Processor Segment Overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating Point Exception",
    "Alignment Check Exception",
    "Machine Check Exception",
    "SIMD Floating-Point Exception",
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

static struct isr_table_entry isrs[ISR_NUM];
static uint8_t isr_counter = 240;

bool isr_allocate_vector(uint8_t* vector) {
    if (isr_counter <= 48) {
        return false;
    }

    *vector = isr_counter;

    do {
        isr_counter--;
    } while (isrs[isr_counter].handler != NULL);

    return true;
}

void isr_register_handler(uint8_t vector, isr_handler_t handler, void* arg) {
    isrs[vector] = (struct isr_table_entry) {
        .handler = handler,
        .arg = arg
    };
}

void isr_unregister_handler(uint8_t vector) {
    isrs[vector] = (struct isr_table_entry) {0};
}

void isr_handler(struct registers* r) {
    uint8_t int_number = r->int_number & 0xff;
    if (int_number == PANIC_IPI_VECTOR) {
        cli();
        for (;;) {
            hlt();
        }
    }

    struct isr_table_entry* entry = &isrs[int_number];

    if (int_number < EXCEPTION_NUM - 1) {
        if (!entry->handler) {
            kpanic(r, false, "unhandled exception: %s", EXCEPTION_MESSAGES[int_number]);
        }

        entry->handler(r, entry->arg);
    } else {
        if (likely(entry->handler != NULL)) {
            entry->handler(r, entry->arg);
        }

        lapic_eoi();
    }
}
