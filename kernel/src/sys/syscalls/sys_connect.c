#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/socket.h>
#include <sys/process.h>
#include <utils/usercopy.h>

void sys_connect(struct registers* r) {
    int fd = r->rdi;
    const struct sockaddr* addr = (const struct sockaddr*) r->rsi;
    socklen_t addr_len = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (addr_len > sizeof(struct sockaddr_storage)) {
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

    struct sockaddr_storage kaddr;
    if ((ret = user_memcpy_from_user(&kaddr, addr, addr_len)) < 0) {
        goto end;
    }

    struct socket_node* socket = (struct socket_node*) file->node;

    ret = socket->sockops->connect(socket, (struct sockaddr*) &kaddr, addr_len);

end:
    file_release(file);

    r->rax = ret;
}
