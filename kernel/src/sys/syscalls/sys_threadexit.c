#include <cpu/isr.h>
#include <sys/scheduler.h>

void sys_threadexit(struct registers* r) {
    (void) r;
    scheduler_thread_exit();
}
