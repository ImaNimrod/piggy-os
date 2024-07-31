#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/ioapic.h> 
#include <dev/lapic.h> 
#include <dev/pit.h>
#include <sys/time.h>

#define PIT_CHANNEL_0_PORT  0x40
#define PIT_CHANNEL_1_PORT  0x41
#define PIT_CHANNEL_2_PORT  0x42
#define PIT_COMMAND_PORT    0x43

#define PIT_INTERNAL_FREQUENCY  1193180

#define PIT_IRQ 0

static void pit_handler(struct registers* r) {
    (void) r;
    time_update_timers();
    lapic_eoi();
}

void pit_init(uint16_t hz) {
    uint16_t divisor = PIT_INTERNAL_FREQUENCY / hz; 

    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL_0_PORT, divisor & 0xff);
    outb(PIT_CHANNEL_0_PORT, (divisor >> 8) & 0xff);

    isr_install_handler(IRQ(PIT_IRQ), pit_handler);
    ioapic_redirect_irq(PIT_IRQ, IRQ(PIT_IRQ));
    ioapic_set_irq_mask(PIT_IRQ, false);
}