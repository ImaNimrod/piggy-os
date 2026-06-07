#include <cpu/isr.h>
#include <sys/scheduler.h>

void sys_yield(struct registers* r) {
    scheduler_yield();
    r->rax = 0;
}
