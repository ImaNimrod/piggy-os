#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/paging.h>
#include <sys/process.h>

void sys_mmap(struct registers* r) {
    void* address = (void*) r->rdi;
    size_t size = r->rsi;
    int prot = r->rdx;
    int flags = r->r10;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (address != NULL && ((uintptr_t) address % PAGE_SIZE_4KB) != 0) {
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

    if (flags & ~(MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS)) {
        r->rax = -EINVAL;
        return;
    }

    if (!(flags & MAP_PRIVATE)) {
        r->rax = -EINVAL;
        return;
    }

    r->rax = (uintptr_t) vmm_map(current_process->vmm_context, (uintptr_t) address, size, prot, flags, 0);
}
