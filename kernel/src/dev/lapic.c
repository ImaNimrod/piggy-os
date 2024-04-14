#include <cpuid.h>
#include <cpu/asm.h>
#include <dev/acpi/madt.h>
#include <dev/lapic.h>
#include <mem/vmm.h>
#include <utils/log.h>

// TODO: setup NMI support
// TODO: maybe do x2apic?

#define LAPIC_REG_ID            0x020
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_SVR           0x0f0
#define LAPIC_REG_CMCI          0x2f0
#define LAPIC_REG_ICR0          0x300
#define LAPIC_REG_ICR1          0x310
#define LAPIC_REG_LVT_TIMER     0x320
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

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_init(uint8_t lapic_id) {
    uint64_t lapic_msr = rdmsr(MSR_LAPIC_BASE);
    wrmsr(MSR_LAPIC_BASE, lapic_msr | (1 << 11));

    lapic_addr = madt_get_lapic_addr();

    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) | (1 << 8));
}

void disable_pic(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}
