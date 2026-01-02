#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/vector.h>

void sys_exit(struct registers* r) {
    int status = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    for (size_t i = 0; i < vector_size(current_process->threads); i++) {
        struct thread* thread = *vector_get(current_process->threads, i);
        if (thread != current_thread) {
            scheduler_dequeue(thread);
        }
    }

    process_exit(current_process, status);

    scheduler_thread_exit();
}
