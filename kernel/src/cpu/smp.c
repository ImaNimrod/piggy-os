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

static size_t cpu_count = 0;
static size_t cpus_initialized = 0;

static void single_cpu_init(struct limine_smp_info* smp_info) {
    gdt_reload();
    idt_reload();
}

// TODO: implement smp
void smp_init(void) {
    struct limine_smp_response* smp_response = smp_request.response;
    cpu_count = smp_response->cpu_count;

    klog("[smp] %lu processors detected\n", cpu_count);

    for (size_t i = 0; i < cpu_count; i++) {
        struct limine_smp_info* cpu = smp_response->cpus[i];

        if (cpu->lapic_id != smp_response->bsp_lapic_id) {
            // cpu->goto_address = single_cpu_init;
        } else {
            idt_init();
            single_cpu_init(cpu);
        }
    }

    while (cpus_initialized != smp_response->cpu_count) {
        pause();
    }
}
