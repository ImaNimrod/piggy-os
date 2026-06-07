#include <cpu/isr.h>
#include <sys/signal.h>

void sys_sigreturn(struct registers* r) {
    signal_restore_signal_frame(r);
}
