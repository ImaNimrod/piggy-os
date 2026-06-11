#ifndef _KERNEL_CPU_ASM_H
#define _KERNEL_CPU_ASM_H

#include <stdint.h>
#include <utils/macros.h>

#define KERNEL_CODE_SEGMENT 0x08
#define KERNEL_DATA_SEGMENT 0x10
#define USER_CODE_SEGMENT   0x23
#define USER_DATA_SEGMENT   0x1b

#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_DF (1ULL << 10)
#define RFLAGS_RF (1ULL << 16)

#define DEFAULT_FCW     0x33f
#define DEFAULT_MXCSR   0x1f80

#define MSR_IA32_APIC_BASE      0x1b
#define MSR_IA32_PAT            0x277
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01
#define MSR_IA32_EFER           0xc0000080
#define MSR_IA32_STAR           0xc0000081
#define MSR_IA32_LSTAR          0xc0000082
#define MSR_IA32_CSTAR          0xc0000083
#define MSR_IA32_SFMASK         0xc0000084
#define MSR_IA32_FS_BASE        0xc0000100
#define MSR_IA32_GS_BASE        0xc0000101
#define MSR_IA32_KERNEL_GS_BASE 0xc0000102

static ALWAYS_INLINE void hlt(void) {
    asm volatile("hlt");
}

static ALWAYS_INLINE void pause(void) {
    asm volatile("pause" ::: "memory");
}

static ALWAYS_INLINE void cli(void) {
    asm volatile("cli" ::: "memory");
}

static ALWAYS_INLINE void sti(void) {
    asm volatile("sti" ::: "memory");
}

static ALWAYS_INLINE bool get_interrupt_state(void) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    return rflags & RFLAGS_IF;
}

static ALWAYS_INLINE void clac(void) {
    asm volatile("clac" ::: "memory");
}

static ALWAYS_INLINE void stac(void) {
    asm volatile("stac" ::: "memory");
}

static ALWAYS_INLINE void swapgs(void) {
    asm volatile("swapgs");
}

static ALWAYS_INLINE void lfence(void) {
    asm volatile("lfence" ::: "memory");
}

static ALWAYS_INLINE void mfence(void) {
    asm volatile("mfence" ::: "memory");
}

static ALWAYS_INLINE void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    asm volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}

static ALWAYS_INLINE uint32_t cpuid_max_leaf(void) {
    static uint32_t cpuid_max;
    if (cpuid_max == 0) {
        asm volatile("cpuid" : "=a"(cpuid_max) : "a"(0) : "rbx", "rcx", "rdx");
    }

    return cpuid_max;
}

static ALWAYS_INLINE uint32_t cpuid_extended_max_leaf(void) {
    static uint32_t cpuid_extended_max;
    if (cpuid_extended_max == 0) {
        asm volatile("cpuid" : "=a"(cpuid_extended_max) : "a"(0x80000000) : "rbx", "rcx", "rdx");
    }

    return cpuid_extended_max;
}

static ALWAYS_INLINE uint32_t cpuid_hypervisor_max_leaf(uint32_t base) {
    static uint32_t cpuid_hypervisor_max;
    if (cpuid_hypervisor_max == 0) {
        asm volatile("cpuid" : "=a"(cpuid_hypervisor_max) : "a"(base) : "rbx", "rcx", "rdx");
    }

    return cpuid_hypervisor_max;
}

static ALWAYS_INLINE uint64_t rdtsc_serialized(void) {
    uint32_t high;
    uint32_t low;
    asm volatile("cpuid; rdtsc;" : "=a"(low), "=d"(high) :: "rbx", "rcx");
    return ((uint64_t) high << 32) | low;
}

static ALWAYS_INLINE void invlpg(uintptr_t vaddr) {
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

static ALWAYS_INLINE uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %%dx, %%al" : "=a"(ret) : "d"(port));
    return ret;
}

static ALWAYS_INLINE uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %%dx, %%ax" : "=a"(ret) : "d"(port));
    return ret;
} 

static ALWAYS_INLINE uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %%dx, %%eax" : "=a"(ret) : "d"(port));
    return ret;
}

static ALWAYS_INLINE void outb(uint16_t port, uint8_t data) {
    asm volatile("outb %%al, %%dx" :: "d"(port), "a"(data));
}

static ALWAYS_INLINE void outw(uint16_t port, uint16_t data) {
    asm volatile("outw %%ax, %%dx" :: "d"(port), "a"(data));
}

static ALWAYS_INLINE void outl(uint16_t port, uint32_t data) {
    asm volatile("outl %%eax, %%dx" :: "d"(port), "a"(data));
}

static ALWAYS_INLINE uint8_t mmio_read8(void* address) {
    uint8_t value;
    asm volatile("movb (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static ALWAYS_INLINE uint16_t mmio_read16(void* address) {
    uint16_t value;
    asm volatile("movw (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static ALWAYS_INLINE uint32_t mmio_read32(void* address) {
    uint32_t value;
    asm volatile("movl (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static ALWAYS_INLINE uint64_t mmio_read64(void* address) {
    uint64_t value;
    asm volatile("movq (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static ALWAYS_INLINE void mmio_write8(void* address, uint8_t value) {
    asm volatile("movb %0, (%1)" : : "r"(value), "r"(address) : "memory");
}

static ALWAYS_INLINE void mmio_write16(void* address, uint16_t value) {
    asm volatile("movw %0, (%1)" : : "r"(value), "r"(address) : "memory");
}

static ALWAYS_INLINE void mmio_write32(void* address, uint32_t value) {
    asm volatile("movl %0, (%1)" : : "r"(value), "r"(address) : "memory");
}

static ALWAYS_INLINE void mmio_write64(void* address, uint64_t value) {
    asm volatile("movq %0, (%1)" : : "r"(value), "r"(address) : "memory");
}

static ALWAYS_INLINE uint64_t read_cr0(void) {
    uint64_t ret;
    asm volatile("mov %%cr0, %0" : "=r"(ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr2(void) {
    uint64_t ret;
    asm volatile("mov %%cr2, %0" : "=r"(ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr3(void) {
    uint64_t ret;
    asm volatile("mov %%cr3, %0" : "=r"(ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE uint64_t read_cr4(void) {
    uint64_t ret;
    asm volatile("mov %%cr4, %0" : "=r"(ret) :: "memory");
    return ret;
}

static ALWAYS_INLINE void write_cr0(uint64_t value) {
    asm volatile("mov %0, %%cr0" :: "r"(value) : "memory");
}

static ALWAYS_INLINE void write_cr3(uint64_t value) {
    asm volatile("mov %0, %%cr3" :: "r"(value) : "memory");
}

static ALWAYS_INLINE void write_cr4(uint64_t value) {
    asm volatile("mov %0, %%cr4" :: "r"(value) : "memory");
}

static ALWAYS_INLINE void write_xcr0(uint64_t value) {
    uint32_t eax = value;
    uint32_t edx = value >> 32;
    asm volatile("xsetbv" :: "a"(eax), "d"(edx), "c"(0) : "memory");
}

static ALWAYS_INLINE uint64_t rdmsr(uint32_t msr) {
    uint32_t eax = 0, edx = 0;
    asm volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

static ALWAYS_INLINE uint64_t wrmsr(uint32_t msr, uint64_t val) {
    uint32_t eax = (uint32_t) val;
    uint32_t edx = (uint32_t) (val >> 32);
    asm volatile("wrmsr" :: "a"(eax), "d"(edx), "c"(msr) : "memory");
    return ((uint64_t) edx << 32) | eax;
}

static ALWAYS_INLINE bool rdrand(uint64_t* val) {
    unsigned char success;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdrand %0; setc %1" : "=r"(*val), "=qm"(success) ::);
        if (success) {
            return true;
        }
    }

    return false;
}

static ALWAYS_INLINE bool rdseed(uint64_t* val) {
    unsigned char success;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdseed %0; setc %1" : "=r"(*val), "=qm"(success) ::);
        if (success) {
            return true;
        }
    }

    return false;
}

#endif /* _KERNEL_CPU_ASM_H */
