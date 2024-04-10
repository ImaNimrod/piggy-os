#include <cpu/idt.h>

struct idt_entry {
    uint16_t offset_low16;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid16;
    uint32_t offset_high32;
uint32_t : 32;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern uintptr_t idt_stubs;

static struct idt_entry idt[256] = {0};

void idt_reload(void) {
    struct idt_ptr idtr = (struct idt_ptr) { sizeof(idt) - 1, (uint64_t) &idt };
    __asm__ volatile("lidtq %0" :: "m" (idtr));
}

void idt_set_gate(uint8_t vector, void* handler, uint8_t ist) {
    uintptr_t ptr = (uintptr_t) handler;

    idt[vector].offset_low16 = (uint16_t) ptr;
    idt[vector].selector = 8;
    idt[vector].ist = ist;

    if (vector < 16) {
        if (((vector < 0x0b) && (vector > 0x06)) || vector == 0x02) {
            idt[vector].flags = 0x8f;
        } else {
            idt[vector].flags = 0xef;
        }
    } else {
        idt[vector].flags = 0x8e;
    }

    idt[vector].offset_mid16 = (uint16_t) (ptr >> 16);
    idt[vector].offset_high32 = (uint32_t) (ptr >> 32);
}

void idt_init(void) {
    void* idt_stub = (void*) &idt_stubs;
    for (size_t i = 0; i < 256; i++) {
        idt_set_gate(i, idt_stub, 0);
        idt_stub = (void*) ((uintptr_t) idt_stub + 32);
    }
}
