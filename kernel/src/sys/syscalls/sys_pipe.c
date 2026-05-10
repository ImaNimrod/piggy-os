#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/pipe.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <utils/usercopy.h>

#define VALID_FLAGS (O_NONBLOCK | O_CLOEXEC)

void sys_pipe(struct registers* r) {
    int* fds = (int*) r->rdi;
    int flags = r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(fds)) {
        r->rax = -EFAULT;
        return;
    }

    if (flags & ~VALID_FLAGS) {
        r->rax = -EINVAL;
        return;
    }

    struct vfs_node* node = NULL;
    struct file* read_half = NULL;
    struct file* write_half = NULL;

    int ret = pipe_create(&node);
    if (ret < 0) {
        r->rax = ret;
        return;
    }

    VFS_NODE_REF(node);

    int kfds[2] = { -1, -1 };

    read_half = file_create(node, O_RDONLY | (flags & ~O_CLOEXEC));
    if (read_half == NULL) {
        r->rax = -ENOMEM;
        goto error;
    }

    ret = file_insert(current_process, read_half, flags & O_CLOEXEC);
    if (ret < 0) {
        r->rax = ret;
        goto error;
    }
    kfds[0] = ret;

    write_half = file_create(node, O_WRONLY | (flags & ~O_CLOEXEC));
    if (write_half == NULL) {
        r->rax = -ENOMEM;
        goto error;
    }

    ret = file_insert(current_process, write_half, flags & O_CLOEXEC);
    if (ret < 0) {
        r->rax = ret;
        goto error;
    }
    kfds[1] = ret;

    ret = user_memcpy_to_user(fds, kfds, sizeof(kfds));
    if (ret < 0) {
        r->rax = ret;
        goto error;
    }

    r->rax = 0;
    return;

error:
    if (kfds[0] >= 0) {
        file_close(current_process, kfds[0]);
    } else {
        if (read_half != NULL) {
            file_release(read_half);
        }
    }

    if (kfds[1] >= 0) {
        file_close(current_process, kfds[1]);
    } else {
        if (write_half != NULL) {
            file_release(write_half);
        }
    }

    if (node != NULL) {
        VFS_NODE_UNREF(node);
        VFS_NODE_UNREF(node);
    }
}
