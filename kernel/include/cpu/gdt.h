#ifndef _KENREL_CPU_GDT_H
#define _KENREL_CPU_GDT_H

#include <stdint.h>

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

void gdt_load_tss(struct tss* tss);
void gdt_reload(void);

#endif /* _KENREL_CPU_GDT_H */
