#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <sys/process.h>

void sys_close(struct registers* r) {
    int fd = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    r->rax = file_close(current_process, fd);
}
