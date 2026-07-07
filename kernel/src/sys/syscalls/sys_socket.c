#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/socket.h>
#include <sys/process.h> 

void sys_socket(struct registers* r) {
    int family = r->rdi;
    int type  = r->rsi;
    int protocol = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    struct vfs_node* node = NULL;

    int ret = socket_create(family, type, protocol, &node);
    if (ret < 0) {
        r->rax = ret;
        return;
    }

    struct file* file = file_create(node, O_RDWR);
    if (!file) {
        VFS_NODE_UNREF(node);
        r->rax = -ENOMEM;
        return;
    }

    ret = file_insert(current_process, file, false);
    if (ret < 0) {
        file_release(file);
        r->rax = ret;
        return;
    }

    r->rax = ret;
}
