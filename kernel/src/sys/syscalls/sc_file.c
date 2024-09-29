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

    int ret = 0;

    USER_ACCESS_BEGIN;

    klog("[syscall] running syscall_open (path: %s, flags: %04o) on (pid: %u, tid: %u)\n",
            path, flags, current_process->pid, current_thread->tid);

    if (!check_user_ptr(path)) {
        ret = -EFAULT;
        goto end;
    }
    if (path == NULL || *path == '\0') {
        ret = -ENOENT;
        goto end;
    }
    if (strlen(path) >= PATH_MAX) {
        ret = -ENAMETOOLONG;
        goto end;
    }

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
        ret = -EINVAL;
        goto end;
    }

    if (node == NULL && (flags & O_CREAT)) {
        node = vfs_create(current_process->cwd, path, S_IFREG);
    }

    if (node == NULL) {
        ret = -ENOENT;
        goto end;
    }

    node = vfs_reduce_node(node);
    if (node == NULL) {
        ret = -ENOENT;
        goto end;
    }

    if (!S_ISDIR(node->stat.st_mode) && flags & O_DIRECTORY) {
        ret = -ENOTDIR;
        goto end;
    }

    struct file_descriptor* fd = fd_create(node, flags);
    if (fd == NULL) {
        ret = -ENOMEM;
        goto end;
    }

    if ((flags & O_TRUNC) && S_ISREG(node->stat.st_mode)) {
        if ((ret = node->truncate(node, 0)) < 0) {
            goto end;
        }
    }

    int fdnum = fd_alloc_fdnum(current_process, fd);
    if (fdnum == -1) {
        kfree(fd);
        node->refcount--;
        ret = -EMFILE;
        goto end;
    }

    if (flags & O_APPEND) {
        current_process->file_descriptors[fdnum]->offset = node->stat.st_size;
    }

    ret = fdnum;

end:
    USER_ACCESS_END;
    r->rax = ret;
}

void syscall_mkdir(struct registers* r) {
    const char* path = (char*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

    USER_ACCESS_BEGIN;

    klog("[syscall] running syscall_mkdir (path: %s) on (pid: %u, tid: %u)\n",
            path, current_process->pid, current_thread->tid);

    if (!check_user_ptr(path)) {
        ret = -EFAULT;
        goto end;
    }

    if (path == NULL || *path == '\0') {
        ret = -ENOENT;
        goto end;
    }

    if (strlen(path) >= PATH_MAX) {
        ret = -ENAMETOOLONG;
        goto end;
    }

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node != NULL) {
        ret = -EEXIST;
        goto end;
    }

    node = vfs_create(current_process->cwd, path, S_IFDIR);
    if (node == NULL) {
        ret = -ENOENT;
        goto end;
    }

    ret = 0;

end:
    USER_ACCESS_END;
    r->rax = ret;
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

    klog("[syscall] running syscall_read (fdnum: %d, buf: 0x%p, count: %zu) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) buf, count, current_process->pid, current_thread->tid);

    if (!check_user_ptr(buf)) {
        r->rax = -EFAULT;
        return;
    }

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }
    if (acc_mode != O_RDWR && acc_mode != O_RDONLY) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    if (S_ISBLK(node->stat.st_mode) && (count % node->stat.st_blksize)) {
        r->rax = -EINVAL;
        return;
    }

    spinlock_acquire(&node->lock);

    USER_ACCESS_BEGIN;
    ssize_t read = node->read(node, buf, fd->offset, count, fd->flags);
    USER_ACCESS_END;

    spinlock_release(&node->lock);

    if (read > 0) {
        fd->offset += read;
    }
    r->rax = read;
}

void syscall_write(struct registers* r) {
    int fdnum = r->rdi;
    const void* buf = (const void*) r->rsi;
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_write (fdnum: %d, buf: 0x%p, count: %zu) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) buf, count, current_process->pid, current_thread->tid);

    if (!check_user_ptr(buf)) {
        r->rax = -EFAULT;
        return;
    }

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }
    if (acc_mode != O_RDWR && acc_mode != O_WRONLY) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    if (S_ISBLK(node->stat.st_mode) && (count % node->stat.st_blksize)) {
        r->rax = -EINVAL;
        return;
    }

    spinlock_acquire(&node->lock);

    USER_ACCESS_BEGIN;
    ssize_t written = node->write(node, buf, fd->offset, count, fd->flags);
    USER_ACCESS_END;

    spinlock_release(&node->lock);

    if (written > 0) {
        fd->offset += written;
    }
    r->rax = written;
}

void syscall_ioctl(struct registers* r) {
    int fdnum = r->rdi;
    uint64_t request = r->rsi;
    void* argp = (void*) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_ioctl (fdnum: %d, request: 0x%x, argp: 0x%p) on (pid: %u, tid: %u)\n",
            fdnum, request, (uintptr_t) argp, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = fd->node;

    spinlock_acquire(&node->lock);
    r->rax = node->ioctl(node, request, argp);
    spinlock_release(&node->lock);
}

void syscall_seek(struct registers* r) {
    int fdnum = r->rdi;
    off_t offset = r->rsi;
    int whence = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_seek (fdnum: %d, offset: %ld, whence: %d) on (pid: %u, tid: %u)\n",
            fdnum, offset, whence, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    if (S_ISBLK(node->stat.st_mode) && (offset % node->stat.st_blksize)) {
        r->rax = -EINVAL;
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

    klog("[syscall] running syscall_truncate (fdnum: %d, length: %ld) on (pid: %u, tid: %u)\n",
            fdnum, length, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }
    if (acc_mode != O_RDWR && acc_mode != O_WRONLY) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = -EISDIR;
        return;
    }

    spinlock_acquire(&node->lock);
    r->rax = node->truncate(node, length);
    spinlock_release(&node->lock);
}

void syscall_fcntl(struct registers* r) {
    int fdnum = r->rdi;
    int cmd = r->rsi;
    int arg = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_fcntl (fdnum: %d, cmd: %d, arg: %d) on (pid: %u, tid: %u)\n",
            fdnum, cmd, arg, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }

    switch (cmd) {
        case F_DUPFD:
            r->rax = fd_dup(current_process, fdnum, current_process, arg, false, false);
            break;
        case F_DUPFD_CLOEXEC:
            r->rax = fd_dup(current_process, fdnum, current_process, arg, false, true);
            break;
        case F_GETFD:
            r->rax = fd->flags & FD_FLAGS_MASK;
            break;
        case F_SETFD:
            fd->flags = arg & FD_FLAGS_MASK;
            r->rax = 0;
            break;
        case F_GETFL:
            r->rax = fd->flags & FILE_CREATION_FLAGS_MASK;
            break;
        case F_SETFL:
            fd->flags = arg & FILE_CREATION_FLAGS_MASK;
            r->rax = 0;
            break;
        default:
            r->rax = -EINVAL;
            break;
    }
}

void syscall_fsync(struct registers* r) {
    int fdnum = r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_fsync (fdnum: %d) on (pid: %u, tid: %u)\n",
            fdnum, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = fd->node;

    spinlock_acquire(&node->lock);
    r->rax = node->sync(node);
    spinlock_release(&node->lock);
}

void syscall_stat(struct registers* r) {
    int fdnum = r->rdi;
    struct stat* stat = (struct stat*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_stat (fdnum: %d, stat: 0x%p) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) stat, current_process->pid, current_thread->tid);

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = fd->node;

    spinlock_acquire(&node->lock);

    if (copy_to_user(stat, &node->stat, sizeof(struct stat)) == NULL) {
        r->rax = -EFAULT;
    } else {
        r->rax = 0;
    }

    spinlock_release(&node->lock);
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

    klog("[syscall] running syscall_getcwd (buffer: 0x%p, length: %zu) on (pid: %u, tid: %u)\n",
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

void syscall_getdents(struct registers* r) {
    int fdnum = r->rdi;
    struct dirent* buf = (struct dirent*) r->rsi;
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    klog("[syscall] running syscall_getdents (fdnum: %d, buf: 0x%p, count: %zu) on (pid: %u, tid: %u)\n",
            fdnum, (uintptr_t) buf, count, current_process->pid, current_thread->tid);

    if (!check_user_ptr(buf)) {
        r->rax = -EFAULT;
        return;
    }

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }
    if (acc_mode != O_RDWR && acc_mode != O_RDONLY) {
        r->rax = -EPERM;
        return;
    }

    struct vfs_node* node = fd->node;

    spinlock_acquire(&node->lock);

    if (!S_ISDIR(node->stat.st_mode)) {
        r->rax = -ENOTDIR;
        spinlock_release(&node->lock);
        return;
    }

    USER_ACCESS_BEGIN;
    ssize_t read = vfs_getdents(node, buf, fd->offset, count);
    USER_ACCESS_END;

    spinlock_release(&node->lock);

    if (read > 0) {
        fd->offset += read;
    }
    r->rax = read;
}
