#include <cpu/idt.h>

struct idt_entry {
    uint16_t offset_low16;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid16;
    uint32_t offset_high32;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256] = {0};
static struct idt_ptr idt_ptr;

void idt_reload(void) {
	__asm__ volatile("lidtq %0" :: "m" (idt_ptr));
}

void idt_init(void) {
    idt_ptr = (struct idt_ptr) {
        .limit = (sizeof(struct idt_entry) * 256) - 1,
        .base = (uint64_t) &idt,
    };
}
