#include <cpu/asm.h>
#include <cpu/smp.h>
#include <cpu/tsc.h>
#include <dev/hpet.h>
#include <mem/slab.h>
#include <utils/cmdline.h> 
#include <utils/log.h> 
#include <utils/macros.h>

static bool tsc_check(void) {
    if (cmdline_get("notsc") != NULL) {
        return false;
    }

    uint32_t edx, unused;

    if (cpuid_extended_max_leaf() < 0x80000007) {
        return false;
    }

    cpuid(0x80000007, 0, &unused, &unused, &unused, &edx);
    return edx & (1 << 8);
}

static struct timer_info* tsc_init(void) {
    uint64_t tsc_frequency;

    bool need_calibration = true;

    uint32_t eax, ebx, ecx, unused;

    if (cpuid_max_leaf() >= 0x15) {
        cpuid(0x15, 0, &eax, &ebx, &ecx, &unused);

        uint64_t frequency = ecx;
        uint64_t num = ebx;
        uint64_t den = eax;

        if (frequency != 0 && num != 0 && den != 0) {
            tsc_frequency = (frequency * num) / den;
            need_calibration = false;
        }
    }

    if (need_calibration) {
        tsc_frequency = hpet_calibrate_tsc();
    }

    klog("[tsc]: initialized invariant TSC (frequency: %luMHz)\n", tsc_frequency / 1000000);

    struct timer_info* info = kmalloc(sizeof(struct timer_info));
    if (unlikely(info == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for TSC timer");
    }
    info->hz = tsc_frequency;

    return info;
}

static uint64_t tsc_ticks(struct timer_info* info) {
    (void) info;
    return rdtsc_serialized();
}

struct timer_driver tsc_driver = {
    .name = "Invariant TSC",
    .priority = 90,
    .bootstrap = false,
    .check = tsc_check,
    .init = tsc_init,
    .ticks = tsc_ticks,
};
