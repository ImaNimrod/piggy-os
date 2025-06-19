#include <cpu/gdt.h>
#include <utils/string.h>

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt bsp_gdt = {
    .null = {0},
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
    .tss = {
        .length = sizeof(struct tss),
        .flags1 = 0x89,
    },
};

void gdt_init(struct gdt* gdt) {
    memcpy(gdt, &bsp_gdt, sizeof(struct gdt));
}

void gdt_reload(struct gdt* gdt) {
    struct gdt_ptr gdtr = {
        sizeof(struct gdt) - 1,
        (uint64_t) gdt,
    };

    asm volatile(
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
        : "m" (gdtr)
        : "rax", "memory"
    );
}

void gdt_set_tss(struct gdt* gdt, struct tss* tss) {
    uintptr_t tss_addr = (uintptr_t) tss;

    gdt->tss = (struct tss_descriptor) {
        .length = sizeof(struct tss),
        .base_low16 = (uint16_t) tss_addr,
        .base_mid8 = (uint8_t) (tss_addr >> 16),
        .flags1 = 0x89,
        .base_high8   = (uint8_t) (tss_addr >> 24),
        .base_upper32 = (uint32_t) (tss_addr >> 32),
    };

    asm volatile("ltrw %0" :: "rm" ((uint16_t) 0x28));
}
