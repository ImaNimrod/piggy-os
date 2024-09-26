#ifndef _KERNEL_CPU_ASM_H
#define _KERNEL_CPU_ASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IA32_APIC_BASE_MSR      0x1b
#define IA32_EFER_MSR           0xc0000080
#define IA32_STAR_MSR           0xc0000081
#define IA32_LSTAR_MSR          0xc0000082
#define IA32_CSTAR_MSR          0xc0000083
#define IA32_SFMASK_MSR         0xc0000084
#define IA32_FS_BASE_MSR        0xc0000100
#define IA32_GS_BASE_MSR        0xc0000101
#define IA32_KERNEL_GS_BASE_MSR 0xc0000102

static inline void cli(void) {
    __asm__ volatile("cli");
}

static inline void sti(void) {
    __asm__ volatile("sti");
}

static inline bool interrupt_state(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=rm" (flags) :: "memory");
    return flags & (1 << 9);
}

static inline void clac(void) {
    __asm__ volatile("clac" ::: "cc");
}

static inline void stac(void) {
    __asm__ volatile("stac" ::: "cc");
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

static inline void fxsave(void* ctx) {
    __asm__ volatile("fxsave (%0)" :: "r"(ctx) : "memory");
}

static inline void fxrstor(void* ctx) {
    __asm__ volatile ("fxrstor (%0)" :: "r"(ctx) : "memory");
}

static inline void xsave(void* ctx) {
    __asm__ volatile ("xsave (%0)" :: "r" (ctx), "a" (0xffffffff), "d" (0xffffffff) : "memory");
}

static inline void xrstor(void* ctx) {
    __asm__ volatile ("xrstor (%0)" :: "r"(ctx), "a"(0xffffffff), "d"(0xffffffff) : "memory");
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

static inline void insw(uint16_t port, uint16_t* buf, size_t count) {
    __asm__ volatile ("cld; rep; insw" : "+D" (buf), "+c" (count) : "d" (port));
}

static inline void outsw(uint16_t port, const uint16_t* buf, size_t count) {
    __asm__ volatile ("cld; rep; outsw" : "+A" (buf), "+c" (count) : "d" (port));
}

static inline bool cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint32_t cpuid_max;
    asm volatile("cpuid" : "=a"(cpuid_max) : "a"(leaf & 0x80000000) : "rbx", "rcx", "rdx");

    if (leaf > cpuid_max) {
        return false;
    }

    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
    return true;
}

static inline uint64_t read_cr0(void) {
    uint64_t ret;
    __asm__ volatile ("mov %%cr0, %0" : "=r" (ret) :: "memory");
    return ret;
}

static inline uint64_t read_cr2(void) {
    uint64_t ret;
    __asm__ volatile ("mov %%cr2, %0" : "=r" (ret) :: "memory");
    return ret;
}

static inline uint64_t read_cr3(void) {
    uint64_t ret;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (ret) :: "memory");
    return ret;
}

static inline uint64_t read_cr4(void) {
    uint64_t ret;
    __asm__ volatile ("mov %%cr4, %0" : "=r" (ret) :: "memory");
    return ret;
}

static inline void write_cr0(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr0" :: "r" (value) : "memory");
}

static inline void write_cr2(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr2" :: "r" (value) : "memory");
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" :: "r" (value) : "memory");
}

static inline void write_cr4(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr4" :: "r" (value) : "memory");
}

static inline void write_xcr0(uint64_t value) {
	uint32_t eax = value;
	uint32_t edx = value >> 32;
	__asm__ volatile("xsetbv" :: "a" (eax), "d" (edx), "c" (0) : "memory");
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

static inline uint64_t rdseed(void) {
    uint64_t result;
    __asm__ volatile ("rdseed %0" : "=r" (result));
    return result;
}

static inline uint64_t rdrand(void) {
    uint64_t result;
    __asm__ volatile ("rdrand %0" : "=r" (result));
    return result;
}

#endif /* _KERNEL_ASM_H */
