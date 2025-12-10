#ifndef _KERNEL_CPU_GDT_H
#define _KERNEL_CPU_GDT_H

#include <stdint.h>

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
} __attribute__((aligned(16), packed));

struct tss {
    uint32_t : 32;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t : 64;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t : 64;
    uint32_t iopb;
} __attribute__((packed));

void gdt_reload(void);

#endif /* _KERNEL_CPU_GDT_H */
