#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <types.h>
#include <utils/spinlock.h>
#include <utils/user_access.h>

// TODO: implement errno system

void syscall_open(struct registers* r) {
    const char* path = (char*) r->rdi;
    int flags = r->rsi;
    mode_t mode = r->rdi;
    struct process* current_process = this_cpu()->running_thread->process;

    if (!check_user_ptr(path)) {
        r->rax = (uint64_t) -1;
        return;
    }

    USER_ACCESS_BEGIN;

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (node == NULL && (flags & O_CREAT)) {
        node = vfs_create(current_process->cwd, path, mode);
    }

    USER_ACCESS_END;

    if (node == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    node = vfs_reduce_node(node);
    if (node == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (!S_ISDIR(node->stat.st_mode) && (flags & O_DIRECTORY) != 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct file_descriptor* fd = fd_create(node, flags);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
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

    if (fd_close(this_cpu()->running_thread->process, fdnum)) {
        r->rax = 0;
    } else {
        r->rax = -1;
    }
}

void syscall_read(struct registers* r) {
    int fdnum = r->rdi;
    void* buf = (void*) r->rsi;
    size_t count = r->rdx;

    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_RDONLY)) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!check_user_ptr(buf)) {
        r->rax = (uint64_t) -1;
        return;
    }

    USER_ACCESS_BEGIN;

    ssize_t read = node->read(node, buf, fd->offset, count);
    if (read < 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    USER_ACCESS_END;

    fd->offset += read;
    r->rax = read;
}

void syscall_write(struct registers* r) {
    int fdnum = r->rdi;
    const void* buf = (const void*) r->rsi;
    size_t count = r->rdx;
    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_WRONLY)) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!check_user_ptr(buf)) {
        r->rax = (uint64_t) -1;
        return;
    }

    USER_ACCESS_BEGIN;

    ssize_t written = node->write(node, buf, fd->offset, count);
    if (written < 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    USER_ACCESS_END;

    fd->offset += written;
    r->rax = written;
}

void syscall_ioctl(struct registers* r) {
    int fdnum = r->rdi;
    uint64_t request = r->rsi;
    void* argp = (void*) r->rdx;
    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = (uint64_t) -1;
        return;
    }

    /* 
     * NOTE: because ioctls are device specific, it's up to individual device drivers to
     * define how the argp argument is used. This means that if argp is a pointer to a struct,
     * it is also the device drivers' responsiblity to safely access that data using
     * the API defined utils/user_access.h .
     */
    r->rax = fd->node->ioctl(fd->node, request, argp);
}

void syscall_seek(struct registers* r) {
    int fdnum = r->rdi;
    off_t offset = r->rsi;
    int whence = r->rdx;

    struct file_descriptor* fd = fd_from_fdnum(this_cpu()->running_thread->process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode) || S_ISCHR(node->stat.st_mode)) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (S_ISBLK(node->stat.st_mode) && (offset % node->stat.st_blksize) != 0) {
        r->rax = (uint64_t) -1;
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
            r->rax = (uint64_t) -1;
            return;
    }

    if (new_offset < 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    fd->offset = new_offset;
    r->rax = new_offset;
}

void syscall_truncate(struct registers* r) {
    int fdnum = r->rdi;
    off_t length = r->rsi;
    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    int acc_mode = fd->flags & O_ACCMODE;
    if (acc_mode & O_PATH || (acc_mode != O_RDWR && acc_mode != O_WRONLY)) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = fd->node;

    if (S_ISDIR(node->stat.st_mode)) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (!node->truncate(node, length)) {
        r->rax = (uint64_t) -1;
    } else {
        r->rax = 0;
    }
}

void syscall_stat(struct registers* r) {
    int fdnum = r->rdi;
    struct stat* stat = (struct stat*) r->rsi;
    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (copy_to_user(stat, &fd->node->stat, sizeof(struct stat)) == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }
    r->rax = 0;
}

void syscall_chdir(struct registers* r) {
    int fdnum = r->rdi;
    struct process* current_process = this_cpu()->running_thread->process;

    struct file_descriptor* fd = fd_from_fdnum(current_process, fdnum);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = fd->node;

    if (!S_ISDIR(node->stat.st_mode)) {
        r->rax = (uint64_t) -1;
        return;
    }

    current_process->cwd = node;
    r->rax = 0;
}

void syscall_getcwd(struct registers* r) {
    char* buffer = (char*) r->rdi;
    size_t len = r->rsi;
    struct process* current_process = this_cpu()->running_thread->process;

    char temp_buffer[PATH_MAX];
    size_t actual_len = vfs_get_pathname(current_process->cwd, temp_buffer, sizeof(temp_buffer) - 1);

    if (actual_len >= len) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (copy_to_user(buffer, temp_buffer, len) == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }
    r->rax = 0;
}
