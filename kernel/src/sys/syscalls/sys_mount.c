#include <cpu/isr.h>
#include <cpu/smp.h>
#include <fs/vfs.h>
#include <sys/process.h>

void sys_mount(struct registers* r) {
    const char* source = (const char*) r->rdi; // TODO: make this safe usercopy
    const char* target = (const char*) r->rsi; // TODO: make this safe usercopy
    const char* fs_name = (const char*) r->rdx; // TODO: make this safe usercopy

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    VFS_NODE_REF(current_process->cwd);

    struct vfs_node* backing_node = NULL;
    if (source != NULL) {
        ret = vfs_lookup(current_process->cwd, source, false, NULL, &backing_node);
        if (ret < 0) {
            goto end;
        }

        backing_node->ops->unlock(backing_node);
    }

    ret = vfs_mount(backing_node, current_process->cwd, target, fs_name);

    if (backing_node != NULL) {
        VFS_NODE_UNREF(backing_node);
    }

end:
    VFS_NODE_UNREF(current_process->cwd);

    r->rax = ret;
}
