#include <cpu/asm.h>
#include <dev/pvclock.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <stddef.h> 
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>

#define CPUID_KVM_BASE 0x40000000

struct pvclock_vcpu_time_info {
    uint32_t version;
    uint32_t: 32;
	uint64_t tsc;
	uint64_t time;
	uint32_t tsc_mul;
	int8_t tsc_shift;
	uint8_t flags;
    uint16_t: 16;
} __attribute__((packed));

static bool pvclock_check(void) {
    if (cmdline_get("nopvclock") != NULL) {
        return false;
    }

    if (cpuid_hypervisor_max_leaf(CPUID_KVM_BASE) < CPUID_KVM_BASE + 1) {
        return false;
    }

    uint32_t eax, unused;
    cpuid(CPUID_KVM_BASE + 1, 0, &eax, &unused, &unused, &unused);

    return eax & (1 << 3);
}

static struct timer_info* pvclock_init(void) {
    void* vaddr = kmalloc(sizeof(struct pvclock_vcpu_time_info));
    if (unlikely(vaddr == NULL)) {
        kpanic(NULL, false, "failed to allocate memory to map KVM pvclock");
    }

    uintptr_t paddr = (uintptr_t) vaddr - HIGH_VMA;

    struct timer_info* info = kmalloc(sizeof(struct timer_info));
    if (unlikely(info == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for KVM pvclock");
    }
    info->hz = 1000000000;
    info->private = (void*) paddr;

    klog("[pvclock] initialized KVM pvclock (frequency: %luMHz)\n", info->hz / 1000000);

    wrmsr(MSR_KVM_SYSTEM_TIME_NEW, paddr | (1 << 0));
    return info;
}

static uint64_t pvclock_ticks(struct timer_info* info) {
    struct pvclock_vcpu_time_info* pvclock_info = (void*) ((uintptr_t) info->private + HIGH_VMA);

    // Thanks linux/arch/x86/kernel/pvclock.c
    uint64_t ticks = 0;
    uint32_t version_old, version_new;

    do {
        do {
            version_old = pvclock_info->version;
        } while (version_old & (1 << 0));

        lfence();

        ticks = rdtsc_serialized() - pvclock_info->tsc;
        if (pvclock_info->tsc_shift >= 0) {
            ticks <<= pvclock_info->tsc_shift;
        } else {
            ticks >>= -pvclock_info->tsc_shift;
        }

        asm volatile("mulq %%rdx; shrd $32, %%rdx, %%rax" : "=a"(ticks) : "a"(ticks), "d"(pvclock_info->tsc_mul));

        ticks += pvclock_info->time;

        version_new = pvclock_info->version;
        lfence();
    } while (version_new & (1 << 0) || version_old != version_new);

    return ticks;
}

struct timer_driver pvclock_driver = {
    .name = "KVM pvclock",
    .priority = 100,
    .bootstrap = true,
    .check = pvclock_check,
    .init = pvclock_init,
    .ticks = pvclock_ticks,
};
