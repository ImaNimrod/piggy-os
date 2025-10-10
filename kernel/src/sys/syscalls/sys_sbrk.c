#include <cpu/isr.h>
#include <cpu/smp.h>
#include <stdint.h>
#include <sys/process.h>

// TODO: stop using shitty BSD sbrk interface
void sys_sbrk(struct registers* r) {
    intptr_t size = r->rdi;
    r->rax = (uint64_t) process_sbrk(this_cpu()->running_thread->process, size);
}
