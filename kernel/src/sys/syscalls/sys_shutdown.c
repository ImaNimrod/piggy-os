#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/socket.h>
#include <sys/process.h>

void sys_shutdown(struct registers* r) {
    int fd = r->rdi;
    int how = r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (how & ~SHUT_RDWR) {
        r->rax = -EINVAL;
        return;
    }

    struct file* file = file_get(current_process, fd);
    if (!file) {
        r->rax = -EBADF;
        return;
    }

    int ret = 0;

    if (file->node->type != VFS_TYPE_SOCKET) {
        ret = -ENOTSOCK;
        goto end;
    }

    struct socket_node* socket = (struct socket_node*) file->node;

    ret = socket->sockops->shutdown(socket, how);

end:
    file_release(file);

    r->rax = ret;
}
