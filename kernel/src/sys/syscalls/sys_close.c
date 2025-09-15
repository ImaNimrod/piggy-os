#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <sys/process.h>

void sys_close(struct registers* r) {
    int fd = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file != NULL) {
        file_release(file);
        r->rax = 0;
    } else {
        r->rax = -EBADF;
    }
}
