#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/vmm.h>
#include <sys/process.h> 

void sys_mprotect(struct registers* r) {
    void* address = (void*) r->rdi;
    size_t size = r->rsi;
    int prot = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (address && ((uintptr_t) address % PAGE_SIZE_4KB) != 0) {
        r->rax = -EINVAL;
        return;
    }
    if (size == 0 || (size % PAGE_SIZE_4KB) != 0) {
        r->rax = -EINVAL;
        return;
    }

    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = vmm_remap(current_process->vmm_context, (uintptr_t) address, size, prot);
}
