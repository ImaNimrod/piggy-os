#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <utils/usercopy.h>

void sys_munmap(struct registers* r) {
    void* address = (void*) r->rdi;
    size_t size = r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(address)) {
        r->rax = -EFAULT;
        return;
    }

    if (size == 0 || ((uintptr_t) address % PAGE_SIZE_4KB) != 0) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = vmm_unmap(current_process->vmm_context, (uintptr_t) address, size);
}
