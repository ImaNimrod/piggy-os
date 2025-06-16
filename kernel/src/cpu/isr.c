#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/lapic.h>
#include <stddef.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define EXCEPTION_NUM 32

struct isr_table_entry {
    isr_handler_t handler;
    void* arg;
};

static const char* exception_messages[EXCEPTION_NUM] = {
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

static struct isr_table_entry isrs[ISR_NUM] = {0};
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
    if (r->cs & 0x03) {
        swapgs();
    }

    uint8_t int_number = r->int_number & 0xff;
    struct isr_table_entry* entry = &isrs[int_number];

    if (int_number == PANIC_IPI_VECTOR) {
        cli();
        for (;;) {
            hlt();
        }
    }

    if (int_number < EXCEPTION_NUM - 1) {
        if (entry->handler == NULL) {
            kpanic(r, false, "unhandled exception: %s", exception_messages[int_number]);
        }

        entry->handler(r, entry->arg);
    } else {
        if (likely(entry->handler != NULL)) {
            entry->handler(r, entry->arg);
        }

        lapic_eoi();
    }

    if (r->cs & 0x03) {
        swapgs();
    }
} 
