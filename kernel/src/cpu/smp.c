#include <cpu/asm.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/smp.h>
#include <limine.h>
#include <utils/log.h>

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

static size_t cpus_initialized = 0;

static void single_cpu_init(void) {
}

// TODO: implement smp
void smp_init(void) {
    struct limine_smp_response* smp_response = smp_request.response;

    klog("[smp] %lu processors detected\n", smp_response->cpu_count);

    while (cpus_initialized != smp_response->cpu_count) {
        pause();
    }
}
