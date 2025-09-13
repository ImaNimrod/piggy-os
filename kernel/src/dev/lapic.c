#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <mem/paging.h>

#define LAPIC_REG_ID            0x020
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0b0
#define LAPIC_REG_LDR           0x0d0
#define LAPIC_REG_DFR           0x0e0
#define LAPIC_REG_SIVR          0x0f0
#define LAPIC_REG_ESR           0x280
#define LAPIC_REG_ICR_LOW       0x300
#define LAPIC_REG_ICR_HIGH      0x310
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_THERMAL   0x330
#define LAPIC_REG_LVT_PERFCNT   0x340
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURCNT  0x390
#define LAPIC_REG_TIMER_DIV     0x3e0

#define LAPIC_LVT_MASK (1 << 16)

#define PORT_PIC1_COMMAND   0x20
#define PORT_PIC1_DATA      0x21
#define PORT_PIC2_COMMAND   0xa0
#define PORT_PIC2_DATA      0xa1

static inline uint32_t lapic_read(uint32_t reg) {
    if (use_x2apic) {
        return rdmsr(0x800 + (reg >> 4));
    }

    return mmio_read32((void*) (bsp_lapic_addr + HIGH_VMA + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    if (use_x2apic) {
        wrmsr(0x800 + (reg >> 4), value);
        return;
    }

    mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + reg), value);
}

static void lapic_timer_calibrate(void) {
    lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0xffffffff);

    hpet_sleep_ns(MS_TO_NS(10));

    lapic_timer_stop();

    this_cpu()->lapic_frequency = (0xffffffff - lapic_read(LAPIC_REG_TIMER_CURCNT)) / 10;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector) {
    // wait for previous IPI to finish delivery
    while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12)) {
        pause();
    }

    uint32_t icr_low = vector | (1 << 14);
    uint32_t icr_high = 0;

    switch (lapic_id) {
        case LAPIC_IPI_SELF:
            icr_low |= (0x01 << 18);
            break;
        case LAPIC_IPI_ALL_CPUS:
            icr_low |= (0x02 << 18);
            break;
        case LAPIC_IPI_ALL_OTHER_CPUS:
            icr_low |= (0x03 << 18);
            break;
        default:
            icr_high |= lapic_id;
            break;
    }

    if (use_x2apic) {
        wrmsr(0x830, ((uint64_t) icr_high << 32) | icr_low);
    } else {
        mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + LAPIC_REG_ICR_HIGH), icr_high);
        mmio_write32((void*) (bsp_lapic_addr + HIGH_VMA + LAPIC_REG_ICR_LOW), icr_low);
    }
}

void lapic_timer_oneshot(uint8_t vector, uint64_t us) {
    lapic_timer_stop();

    uint64_t ticks = us * (this_cpu()->lapic_frequency / 1000000);
    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, ticks);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, (1 << 16));
}

void lapic_init(void) {
    lapic_write(LAPIC_REG_SIVR, lapic_read(LAPIC_REG_SIVR) | (1 << 8));

    if (!use_x2apic) {
        lapic_write(LAPIC_REG_DFR, 0xf0000000);
        lapic_write(LAPIC_REG_LDR, lapic_read(LAPIC_REG_ID));
    }

    lapic_write(LAPIC_REG_LVT_THERMAL, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_PERFCNT, LAPIC_LVT_MASK);
    lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASK);

    lapic_timer_calibrate();

    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    lapic_eoi();
}

void legacy_pic_disable(void) {
    /* mask all PIC interrupts */
    outb(PORT_PIC1_DATA, 0xff);
    outb(PORT_PIC2_DATA, 0xff);
    /* then remap PIC interrupts to 0x20 - 0x30 to avoid conflicts with builtin CPU exceptions */
    outb(PORT_PIC1_COMMAND, 0x11);
    outb(PORT_PIC2_COMMAND, 0x11);
    outb(PORT_PIC1_DATA, 0x20);
    outb(PORT_PIC2_DATA, 0x28);
    outb(PORT_PIC1_DATA, 0x04);
    outb(PORT_PIC2_DATA, 0x02);
    outb(PORT_PIC1_DATA, 0x01);
    outb(PORT_PIC2_DATA, 0x01);
}
