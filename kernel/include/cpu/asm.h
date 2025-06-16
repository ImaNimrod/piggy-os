#ifndef _KERNEL_CPU_ASM_H
#define _KERNEL_CPU_ASM_H 1

#include <stdbool.h>
#include <stdint.h>
#include <utils/macros.h>

#define IA32_EFER_MSR           0xc0000080
#define IA32_STAR_MSR           0xc0000081
#define IA32_LSTAR_MSR          0xc0000082
#define IA32_CSTAR_MSR          0xc0000083
#define IA32_SFMASK_MSR         0xc0000084
#define IA32_FS_BASE_MSR        0xc0000100
#define IA32_GS_BASE_MSR        0xc0000101
#define IA32_KERNEL_GS_BASE_MSR 0xc0000102

static ALWAYS_INLINE void hlt(void) {
    asm volatile("hlt");
}

static ALWAYS_INLINE void pause(void) {
    asm volatile("pause" ::: "memory");
}

static ALWAYS_INLINE void cli(void) {
    asm volatile("cli");
}

static ALWAYS_INLINE void sti(void) {
    asm volatile("sti");
}

static ALWAYS_INLINE void swapgs(void) {
    asm volatile("swapgs");
}

static ALWAYS_INLINE bool cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint32_t cpuid_max;
    asm volatile("cpuid" : "=a" (cpuid_max) : "a" (leaf & 0x80000000) : "rbx", "rcx", "rdx");

    if (leaf > cpuid_max) {
        return false;
    }

    asm volatile("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx) : "a" (leaf), "c" (subleaf));
    return true;
}

static ALWAYS_INLINE void invlpg(uintptr_t vaddr) {
    asm volatile("invlpg %0" :: "m" ((*((int(*)[])((void*) vaddr)))) : "memory");
}

static ALWAYS_INLINE uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %%dx, %%al" : "=a" (ret) : "d" (port));
    return ret;
}

static ALWAYS_INLINE uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %%dx, %%ax" : "=a" (ret) : "d" (port));
    return ret;
} 

static ALWAYS_INLINE uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %%dx, %%eax" : "=a" (ret) : "d" (port));
    return ret;
}

static ALWAYS_INLINE void outb(uint16_t port, uint8_t data) {
    asm volatile("outb %%al, %%dx" :: "d" (port), "a" (data));
}

static ALWAYS_INLINE void outw(uint16_t port, uint16_t data) {
    asm volatile("outw %%ax, %%dx" :: "d" (port), "a" (data));
}

static ALWAYS_INLINE void outl(uint16_t port, uint32_t data) {
    asm volatile("outl %%eax, %%dx" :: "d" (port), "a" (data));
}

static ALWAYS_INLINE uint64_t read_cr0(void) {
    uint64_t ret;
    asm volatile ("mov %%cr0, %0" : "=r" (ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr2(void) {
    uint64_t ret;
    asm volatile ("mov %%cr2, %0" : "=r" (ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr3(void) {
    uint64_t ret;
    asm volatile ("mov %%cr3, %0" : "=r" (ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr4(void) {
    uint64_t ret;
    asm volatile ("mov %%cr4, %0" : "=r" (ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE void write_cr0(uint64_t value) {
    asm volatile ("mov %0, %%cr0" :: "r" (value) : "memory");
}

static ALWAYS_INLINE void write_cr3(uint64_t value) {
    asm volatile ("mov %0, %%cr3" :: "r" (value) : "memory");
}

static ALWAYS_INLINE void write_cr4(uint64_t value) {
    asm volatile ("mov %0, %%cr4" :: "r" (value) : "memory");
}

static ALWAYS_INLINE void write_xcr0(uint64_t value) {
    uint32_t eax = value;
    uint32_t edx = value >> 32;
    asm volatile("xsetbv" :: "a" (eax), "d" (edx), "c" (0) : "memory");
}

static ALWAYS_INLINE uint64_t rdmsr(uint32_t msr) {
    uint32_t eax = 0, edx = 0;
    asm volatile("rdmsr" : "=a" (eax), "=d" (edx) : "c" (msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

static ALWAYS_INLINE uint64_t wrmsr(uint32_t msr, uint64_t val) {
    uint32_t eax = (uint32_t) val;
    uint32_t edx = (uint32_t) (val >> 32);
    asm volatile("wrmsr" :: "a" (eax), "d" (edx), "c" (msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

static inline void fxsave(void* ctx) {
    asm volatile("fxsave (%0)" :: "r" (ctx) : "memory");
}

static inline void fxrstor(void* ctx) {
    asm volatile ("fxrstor (%0)" :: "r" (ctx) : "memory");
}

static inline void xsave(void* ctx) {
    asm volatile ("xsave (%0)" :: "r" (ctx), "a" (0xffffffff), "d" (0xffffffff) : "memory");
}

static inline void xrstor(void* ctx) {
    asm volatile ("xrstor (%0)" :: "r" (ctx), "a" (0xffffffff), "d" (0xffffffff) : "memory");
}

static inline uint64_t rdfsbase(void) {
    uint64_t fs;
    asm volatile ("rdfsbase %0" : "=r" (fs) :: "memory");
    return fs;
}

static inline void wrfsbase(uint64_t fs) {
    asm volatile ("wrfsbase %0" :: "r" (fs) : "memory");
}

static inline uint64_t rdgsbase(void) {
    uint64_t gs;
    asm volatile ("rdgsbase %0" : "=r" (gs) :: "memory");
    return gs;
}

static inline void wrgsbase(uint64_t gs) {
    asm volatile ("wrgsbase %0" :: "r" (gs) : "memory");
}

#endif /* _KERNEL_CPU_ASM_H */
