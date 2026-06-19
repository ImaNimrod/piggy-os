#include <cpu/gdt.h>
#include <cpu/smp.h>
#include <utils/string.h>

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt template = {
    .null = {},
    .kernel_code64 = {
        .access = 0x9a,
        .granularity = 0x20,
    },
    .kernel_data64 = {
        .access = 0x92,
    },
    .user_data64 = {
        .access = 0xf2,
    },
    .user_code64 = {
        .access = 0xfa,
        .granularity = 0x20,
    },
    .tss = {},
};

void gdt_reload(void) {
    memcpy64((uint64_t*) &this_cpu()->gdt, (uint64_t*) &template, sizeof(struct gdt) >> 3);

    uintptr_t tss_addr = (uintptr_t) &this_cpu()->tss;

    this_cpu()->gdt.tss = (struct tss_descriptor) {
        .length = sizeof(struct tss),
        .base_low16 = (uint16_t) tss_addr,
        .base_mid8 = (uint8_t) (tss_addr >> 16),
        .flags1 = 0x89,
        .base_high8   = (uint8_t) (tss_addr >> 24),
        .base_upper32 = (uint32_t) (tss_addr >> 32),
    };

    struct gdt_ptr gdtr = {
        sizeof(struct gdt) - 1,
        (uint64_t) &this_cpu()->gdt,
    };

    asm volatile("lgdtq %0" :: "m"(gdtr) : "memory");
    asm volatile("ltrw %%ax" :: "a"(0x28));
    asm volatile(
        "swapgs;"
        "mov $0, %%ax;"
        "mov %%ax, %%fs;"
        "mov %%ax, %%gs;"
        "swapgs;"
        "mov $0x10, %%ax;"
        "mov %%ax, %%ds;"
        "mov %%ax, %%es;"
        "mov %%ax, %%ss;"
        "pushq $0x8;"
        "pushq $.reload;"
        "retfq;"
        ".reload:"
        ::: "ax"
    );
}
