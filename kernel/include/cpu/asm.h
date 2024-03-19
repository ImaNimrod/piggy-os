#ifndef _KERNEL_ASM_H
#define _KERNEL_ASM_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %%al, %%dx" :: "d" (port), "a" (data));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %%dx, %%al" : "=a" (ret) : "d" (port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %%ax, %%dx" :: "d" (port), "a" (data));
}

static inline uint16_t inw(uint16_t port){
    uint16_t ret;
    __asm__ volatile("inw %%dx, %%ax" : "=a" (ret) : "d" (port));
    return ret;
} 

static inline void outl(uint16_t port, uint32_t data) {
    __asm__ volatile("outl %%eax, %%dx" :: "d" (port), "a" (data));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %%dx, %%eax" : "=a" (ret) : "d" (port));
    return ret;
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t eax = 0, edx = 0;
    __asm__ volatile("rdmsr" : "=a" (eax), "=d" (edx) : "c" (msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

static inline uint64_t wrmsr(uint32_t msr, uint64_t val) {
    uint32_t eax = (uint32_t) val;
    uint32_t edx = (uint32_t) (val >> 32);
    __asm__ volatile("wrmsr" :: "a" (eax), "d" (edx), "c" (msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

#endif /* _KERNEL_ASM_H */
