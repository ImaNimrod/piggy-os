#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h> 
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_unmount(struct registers* r) {
    const char* target = (const char*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t target_len;
    if ((ret = user_strlen(target, &target_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* ktarget = kmalloc(target_len + 1);
    if (unlikely(ktarget == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    VFS_NODE_REF(current_process->cwd);
    r->rax = vfs_unmount(current_process->cwd, ktarget);
    VFS_NODE_UNREF(current_process->cwd);

    kfree(ktarget);
}
