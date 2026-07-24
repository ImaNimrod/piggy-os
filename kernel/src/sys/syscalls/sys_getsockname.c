#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/socket.h>
#include <sys/process.h>
#include <utils/macros.h> 
#include <utils/usercopy.h> 

void sys_getsockname(struct registers* r) {
    int fd = r->rdi;
    struct sockaddr* addr = (struct sockaddr*) r->rsi;
    socklen_t max_addr_len = r->rdx;
    socklen_t* addr_len = (socklen_t*) r->r10;

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

    struct sockaddr_storage kaddr;

    if (max_addr_len > sizeof(kaddr)) {
        ret = -EINVAL;
        goto end;
    }

    ssize_t actual_len = socket->sockops->getpeername(socket, (struct sockaddr*) &kaddr, max_addr_len);
    if (actual_len < 0) {
        ret = actual_len;
        goto end;
    }

    if ((ret = user_memcpy_to_user(addr, &kaddr, actual_len)) < 0) {
        goto end;
    }

    if ((ret = user_memcpy_to_user(addr_len, &actual_len, sizeof(actual_len))) < 0) {
        goto end;
    }

end:
    file_release(file);

    r->rax = ret;
}
