#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <sys/process.h>

void sys_unlink(struct registers* r) {
    const char* path = (const char*) r->rdi; // TODO: make this safe usercopy

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    VFS_NODE_REF(current_process->cwd);
    r->rax = vfs_unlink(current_process->cwd, path);
    VFS_NODE_UNREF(current_process->cwd);
}
