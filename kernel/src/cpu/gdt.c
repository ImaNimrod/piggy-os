#include <cpu/gdt.h>

struct gdt_descriptor {
    uint16_t limit;
    uint16_t base_low16;
    uint8_t base_mid8;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high8;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t length;
    uint16_t base_low16;
    uint8_t base_mid8;
    uint8_t flags1;
    uint8_t flags2;
    uint8_t base_high8;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

struct gdt {
    struct gdt_descriptor null;
    struct gdt_descriptor kernel_code64;
    struct gdt_descriptor kernel_data64;
    struct gdt_descriptor user_code64;
    struct gdt_descriptor user_data64;
    struct tss_descriptor tss;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt gdt;
static struct gdt_ptr gdt_ptr;

void gdt_reload(void) {
    __asm__ volatile(
        "lgdtq %0\n\t"
        "push $0x08\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "push %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%eax\n\t"
        "mov %%eax, %%ds\n\t"
        "mov %%eax, %%es\n\t"
        "mov %%eax, %%fs\n\t"
        "mov %%eax, %%gs\n\t"
        "mov %%eax, %%ss\n\t"
        :
        : "m" (gdt_ptr)
        : "rax", "memory"
    );
}

void gdt_init(void) {
    gdt.null = (struct gdt_descriptor) {0};
    gdt.kernel_code64 = (struct gdt_descriptor) {
        .access = 0x9a,
        .granularity = 0x20,
    };
    gdt.kernel_data64 = (struct gdt_descriptor) {
        .access = 0x92,
    };
    gdt.user_code64 = (struct gdt_descriptor) {
        .access = 0xfa,
        .granularity = 0x20,
    };
    gdt.user_data64 = (struct gdt_descriptor) {
        .access = 0xf2,
    };
    gdt.tss = (struct tss_descriptor) {
        .length = 104,
        .flags1 = 0x89,
    };

    gdt_ptr = (struct gdt_ptr) {
        .limit = sizeof(struct gdt) - 1,
        .base = (uint64_t) &gdt,
    };
}
