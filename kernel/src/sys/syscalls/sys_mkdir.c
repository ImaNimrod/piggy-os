#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h> 
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_mkdir(struct registers* r) {
    const char* path = (const char*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t path_len;
    if ((ret = user_strlen(path, &path_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* kpath = kmalloc(path_len + 1);
    if (unlikely(kpath == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    VFS_NODE_REF(current_process->cwd);
    r->rax = vfs_create(current_process->cwd, kpath, VFS_TYPE_DIRECTORY, NULL);
    VFS_NODE_UNREF(current_process->cwd);

    kfree(kpath);
}
