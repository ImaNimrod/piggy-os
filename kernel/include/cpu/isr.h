#ifndef _KERNEL_CPU_ISR_H
#define _KERNEL_CPU_ISR_H 1

#include <stdbool.h>
#include <stdint.h>

#define ISA_IRQ_BASE    32
#define ISA_IRQ_NUM     16
#define ISR_NUM         256

struct registers {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t int_number;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

typedef void (*isr_handler_t)(struct registers*, void*);

bool isr_allocate_vector(uint8_t* vector);
void isr_register_handler(uint8_t vector, isr_handler_t handler, void* arg);
void isr_unregister_handler(uint8_t vector);

#endif /* _KERNEL_CPU_ISR_H */
