#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/scheduler.h>

void sys_threadnew(struct registers* r) {
    void* entry = (void*) r->rdi;
    void* stack = (void*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct thread* new_thread = thread_create_user(current_process, (uintptr_t) entry, (uintptr_t) stack);
    if (new_thread == NULL) {
        r->rax = -ENOMEM;
        return;
    }

    scheduler_enqueue(new_thread);
    r->rax = new_thread->tid;
}
