#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/ioapic.h> 
#include <dev/pit.h>
#include <stddef.h>
#include <sys/timer.h>

#define PIT_CHANNEL0_PORT   0x40
#define PIT_CHANNEL1_PORT   0x41
#define PIT_CHANNEL2_PORT   0x42
#define PIT_COMMAND_PORT    0x43

#define PIT_INTERNAL_FREQUENCY  1193180

#define PIT_ISA_IRQ 0

static void pit_irq_handler(struct registers* r, void* arg) {
    (void) r;
    (void) arg;
    timer_update_timers();
}

void pit_init(uint16_t hz) {
    uint16_t divisor = PIT_INTERNAL_FREQUENCY / hz; 
    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, divisor & 0xff);
    outb(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xff);

    isr_register_handler(PIT_ISA_IRQ + ISA_IRQ_BASE, pit_irq_handler, NULL);
    ioapic_redirect_irq(PIT_ISA_IRQ, PIT_ISA_IRQ + ISA_IRQ_BASE);
    ioapic_set_irq_mask(PIT_ISA_IRQ, false);
}
