#include <cpu/isr.h>
#include <cpu/smp.h>
#include <fs/vfs.h>
#include <sys/process.h>

void sys_unmount(struct registers* r) {
    const char* target = (const char*) r->rsi; // TODO: make this safe usercopy

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    VFS_NODE_REF(current_process->cwd);
    r->rax = vfs_unmount(current_process->cwd, target);
    VFS_NODE_UNREF(current_process->cwd);
}
