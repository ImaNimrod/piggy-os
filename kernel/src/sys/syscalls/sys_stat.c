#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <types.h>

void sys_stat(struct registers* r) {
    int fd = r->rdi;
    struct stat* stat = (struct stat*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = file->node;

    node->ops->lock(node);
    r->rax = node->ops->getstat(node, stat);
    node->ops->unlock(node);

    file_release(file);
}
