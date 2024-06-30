#include <cpuid.h>
#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <dev/acpi/madt.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <mem/vmm.h>
#include <utils/log.h>

// TODO: setup NMI support
// TODO: maybe do x2apic?

#define LAPIC_REG_ID            0x020
#define LAPIC_REG_VER           0x030
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_SVR           0x0f0
#define LAPIC_REG_CMCI          0x2f0
#define LAPIC_REG_ICR0          0x300
#define LAPIC_REG_ICR1          0x310
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_THERMAL   0x330
#define LAPIC_REG_LVT_PERFCNT   0x340
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURCNT  0x390
#define LAPIC_REG_TIMER_DIV     0x3e0

static uintptr_t lapic_addr = 0;

static uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*) ((uintptr_t) lapic_addr + HIGH_VMA + reg));
}

static void lapic_write(uint32_t reg, uint32_t value) {
    *((volatile uint32_t*) ((uintptr_t) lapic_addr + HIGH_VMA + reg)) = value;
}

static void lapic_timer_calibrate(void) {
    const uint32_t init_ticks = 0xffffffff;

	lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, init_ticks);

    hpet_sleep_ms(10);

    lapic_timer_stop();

	this_cpu()->lapic_frequency = (init_ticks - lapic_read(LAPIC_REG_TIMER_CURCNT)) / 10;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint32_t lapic_id, uint32_t vector) {
    lapic_write(LAPIC_REG_ICR1, lapic_id << 24);
    lapic_write(LAPIC_REG_ICR0, vector);
}

void lapic_timer_oneshot(uint8_t vector, uint64_t us) {
    bool prev_int = interrupt_state();
    cli();

    lapic_timer_stop();

    uint64_t ticks = us * (this_cpu()->lapic_frequency / 1000000);
    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, ticks);

    if (prev_int) {
        sti();
    }
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, 0x10000);
}

void lapic_init(void) {
    uint64_t lapic_msr = rdmsr(MSR_LAPIC_BASE);
    wrmsr(MSR_LAPIC_BASE, lapic_msr | (1 << 11));

    lapic_addr = madt_get_lapic_addr();

    lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) | (1 << 8));

    // TODO: dont just mask NMIs and actually setup NMI support
    lapic_write(LAPIC_REG_LVT_LINT0, 0x10000);
    lapic_write(LAPIC_REG_LVT_LINT1, 0x10000);

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_TPR, 0);
}

void disable_pic(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}
