#include <cpu/isr.h>
#include <cpu/smp.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/vector.h>

void sys_exit(struct registers* r) {
    int status = r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    for (size_t i = 0; i < vector_size(current_process->threads); i++) {
        struct thread* t = *vector_get(current_process->threads, i);
        if (t != current_thread) {
            scheduler_dequeue(&t->cpu->scheduler, t);
        }
    }

    process_exit(current_process, status);
    scheduler_thread_exit();
}
