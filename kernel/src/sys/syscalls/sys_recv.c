#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/socket.h>
#include <sys/process.h>

void sys_recv(struct registers* r) {
    int fd = r->rdi;
    void* buf = (void*) r->rsi;
    size_t count = r->rdx;
    struct sockaddr* addr = (struct sockaddr*) r->r10;
    socklen_t* addr_len = (socklen_t*) r->r8;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

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

    ret = socket->sockops->recv(socket, buf, count, addr, addr_len);

end:
    file_release(file);

    r->rax = ret;
}
