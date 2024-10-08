#include <cpu/gdt.h>
#include <mem/vmm.h>
#include <utils/spinlock.h>

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
    uint32_t : 32;
} __attribute__((packed));

struct gdt {
    struct gdt_descriptor null;
    struct gdt_descriptor kernel_code64;
    struct gdt_descriptor kernel_data64;
    struct gdt_descriptor user_data64;
    struct gdt_descriptor user_code64;
    struct tss_descriptor tss;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt gdt = {
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

static spinlock_t gdt_lock = {0};

UNMAP_AFTER_INIT void gdt_load_tss(struct tss* tss) {
    uintptr_t tss_addr = (uintptr_t) tss;

    spinlock_acquire(&gdt_lock);

    gdt.tss = (struct tss_descriptor) {
        .length = sizeof(struct tss),
        .base_low16 = (uint16_t) tss_addr,
        .base_mid8 = (uint8_t) (tss_addr >> 16),
        .flags1 = 0x89,
        .base_high8   = (uint8_t) (tss_addr >> 24),
        .base_upper32 = (uint32_t) (tss_addr >> 32)
    };

    __asm__ volatile("ltrw %0" :: "rm" ((uint16_t) 0x28) : "memory");

    spinlock_release(&gdt_lock);
}

UNMAP_AFTER_INIT void gdt_reload(void) {
    struct gdt_ptr gdtr = (struct gdt_ptr) { sizeof(gdt) - 1, (uint64_t) &gdt };
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
        : "m" (gdtr)
        : "rax", "memory"
    );
}
