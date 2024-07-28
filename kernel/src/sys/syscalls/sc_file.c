#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <errno.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <types.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/user_access.h>

void syscall_open(struct registers* r) {
    const char* path = (char*) r->rdi;
    int flags = r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    USER_ACCESS_BEGIN;

    klog("[syscall] running syscall_open (path: %s, flags: %04o) on (pid: %u, tid: %u)\n",
            path, flags, current_process->pid, current_thread->tid);

    if (!check_user_ptr(path)) {
        r->rax = -EFAULT;
        return;
    }

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
        r->rax = -EINVAL;
        return;
    }

    if (node == NULL && (flags & O_CREAT)) {
        node = vfs_create(current_process->cwd, path, S_IFREG);
    }

    USER_ACCESS_END;

    if (node == NULL) {
        r->rax = -ENOENT;
        return;
    }

    node = vfs_reduce_node(node);
    if (node == NULL) {
        r->rax = -ENOENT;
        return;
    }

    if (!S_ISDIR(node->stat.st_mode) && (flags & O_DIRECTORY) != 0) {
        r->rax = -ENOTDIR;
        return;
    }

    struct file_descriptor* fd = fd_create(node, flags);
    if (fd == NULL) {
        r->rax = -ENFILE;
        return;
    }

    if ((flags & O_TRUNC) && S_ISREG(node->stat.st_mode)) {
        node->truncate(node, 0);
    }

    int fdnum = fd_alloc_fdnum(current_process, fd);
    if (fdnum == -1) {
        kfree(fd);
        node->refcount--;
    }

    if (flags & O_APPEND) {
        current_process->file_descriptors[fdnum]->offset = node->stat.st_size;
    }

    r->rax = fdnum;
}

void syscall_close(struct registers* r) {
    int fdnum = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_close (fdnum: %d) on (pid: %u, tid: %u)\n",
            fdnum, current_process->pid, current_thread->tid);

    r->rax = fd_close(current_process, fdnum);
}

void syscall_read(struct registers* r) {
    int fdnum = r->rdi;
    void* buf = (void*) r->rsi;
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_read (fdnum: %d, buf: 0x%x, count: %u) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) buf, count, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_RDONLY)) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!check_user_ptr(buf)) {
        r->rax = -EFAULT;
        return;
    }

    USER_ACCESS_BEGIN;
    ssize_t read = node->read(node, buf, fd->offset, count, fd->flags);
    USER_ACCESS_END;

    fd->offset += read;
    r->rax = read;
}

void syscall_write(struct registers* r) {
    int fdnum = r->rdi;
    const void* buf = (const void*) r->rsi;
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_write (fdnum: %d, buf: 0x%x, count: %u) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) buf, count, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_WRONLY)) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!check_user_ptr(buf)) {
        r->rax = -EFAULT;
        return;
    }

    USER_ACCESS_BEGIN;
    ssize_t written = node->write(node, buf, fd->offset, count, fd->flags);
    USER_ACCESS_END;

    fd->offset += written;
    r->rax = written;
}

void syscall_ioctl(struct registers* r) {
    int fdnum = r->rdi;
    uint64_t request = r->rsi;
    void* argp = (void*) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_ioctl (fdnum: %d, request: 0x%x, argp: 0x%x) on (pid: %u, tid: %u)\n",
            fdnum, request, (uintptr_t) argp, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EPERM;
        return;
    }

    USER_ACCESS_BEGIN;
    r->rax = fd->node->ioctl(fd->node, request, argp);
    USER_ACCESS_END;
}

void syscall_seek(struct registers* r) {
    int fdnum = r->rdi;
    off_t offset = r->rsi;
    int whence = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_seek (fdnum: %d, offset: %d, whence: %d) on (pid: %u, tid: %u)\n",
            fdnum, offset, whence, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    if (S_ISBLK(node->stat.st_mode) && (offset % node->stat.st_blksize) != 0) {
        r->rax = -ESPIPE;
        return;
    }

    off_t current_offset = fd->offset;
    off_t new_offset = 0;

    switch (whence) {
        case SEEK_CUR:
            new_offset = current_offset + offset;
            break;
        case SEEK_END:
            new_offset = offset + node->stat.st_size;
            break;
        case SEEK_SET:
            new_offset = offset;
            break;
        default:
            r->rax = -EINVAL;
            return;
    }

    if (new_offset < 0) {
        r->rax = -ESPIPE;
        return;
    }

    fd->offset = new_offset;
    r->rax = new_offset;
}

void syscall_truncate(struct registers* r) {
    int fdnum = r->rdi;
    off_t length = r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_truncate (fdnum: %d, length: %d) on (pid: %u, tid: %u)\n",
            fdnum, length, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_WRONLY)) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    r->rax = node->truncate(node, length);
}

void syscall_stat(struct registers* r) {
    int fdnum = r->rdi;
    struct stat* stat = (struct stat*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_stat (fdnum: %d, stat: 0x%x) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) stat, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    if (copy_to_user(stat, &fd->node->stat, sizeof(struct stat)) == NULL) {
        r->rax = -EFAULT;
    } else {
        r->rax = 0;
    }
}

void syscall_chdir(struct registers* r) {
    int fdnum = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_chdir (fdnum: %d) on (pid: %u, tid: %u)\n",
            fdnum, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!S_ISDIR(node->stat.st_mode)) {
        r->rax = -ENOTDIR;
        return;
    }

    current_process->cwd = node;
    r->rax = 0;
}

void syscall_getcwd(struct registers* r) {
    char* buffer = (char*) r->rdi;
    size_t length = r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getcwd (buffer: 0x%x, length: %d) on (pid: %u, tid: %u)\n",
            (uintptr_t) buffer, length, current_process->pid, current_thread->tid);

    char temp_buffer[PATH_MAX];
    size_t actual_length = vfs_get_pathname(current_process->cwd, temp_buffer, sizeof(temp_buffer) - 1);

    if (actual_length >= length) {
        r->rax = (uint64_t) NULL;
        return;
    }

    if (copy_to_user(buffer, temp_buffer, length) == NULL) {
        r->rax = (uint64_t) NULL;
        return;
    }

    r->rax = (uint64_t) buffer;
}
