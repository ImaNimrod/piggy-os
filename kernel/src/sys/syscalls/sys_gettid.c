#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>

void sys_gettid(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    r->rax = current_thread->tid;
}
