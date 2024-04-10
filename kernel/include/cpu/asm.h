#ifndef _KERNEL_CPU_ASM_H
#define _KERNEL_CPU_ASM_H

#include <stdint.h>

#define MSR_LAPIC_BASE  0x1b
#define MSR_MSR_EFER    0xc0000080
#define MSR_FS_BASE     0xc0000101
#define MSR_KERNEL_GS   0xc0000101
#define MSR_USER_GS     0xc0000102

static inline void cli(void) {
    __asm__ volatile("cli");
}

static inline void sti(void) {
    __asm__ volatile("sti");
}

static inline void hlt(void) {
    __asm__ volatile("hlt");
}

static inline void pause(void) {
    __asm__ volatile("pause");
}

static inline void swapgs(void) {
    __asm__ volatile("swapgs" ::: "memory");
}

static inline void invlpg(uintptr_t vaddr) {
	__asm__ volatile("invlpg %0" :: "m" ((*((int(*)[])((void*) vaddr)))) : "memory");
}

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
