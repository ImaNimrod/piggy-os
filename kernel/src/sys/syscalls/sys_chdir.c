#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>

void sys_chdir(struct registers* r) {
    int fd = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    int ret = 0;

    struct vfs_node* node = file->node;
    if (node->type == VFS_TYPE_DIRECTORY) {
        ret = -EISDIR;
        goto end;
    }

    VFS_NODE_UNREF(current_process->cwd);
    current_process->cwd = node;
    VFS_NODE_REF(current_process->cwd);

end:
    file_release(file);
    r->rax = ret;
}
