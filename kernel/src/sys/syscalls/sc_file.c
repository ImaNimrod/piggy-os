#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <types.h>
#include <utils/spinlock.h>
#include <utils/string.h>

// TODO: implement errno system

void syscall_open(struct registers* r) {
    const char* path = (char*) r->rdi;
    int flags = r->rsi;

    struct process* current_process = this_cpu()->running_thread->process;

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (node == NULL && (flags & O_CREAT)) {
        node = vfs_create(current_process->cwd, path, VFS_NODE_REGULAR);
    }

    if (node == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    node = vfs_reduce_node(node);
    if (node == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (node->type != VFS_NODE_DIRECTORY && (flags & O_DIRECTORY) != 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct file_descriptor* fd = fd_create(node, flags);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if ((flags & O_TRUNC) && node->type == VFS_NODE_REGULAR) {
        node->truncate(node, 0);
    }

    int fdnum = fd_alloc_fdnum(current_process, fd);
    if (fdnum == -1) {
        kfree(fd);
        node->refcount--;
    }

    if (flags & O_APPEND) {
        current_process->file_descriptors[fdnum]->offset = node->size;
    }

    r->rax = fdnum;
}

void syscall_close(struct registers* r) {
    int fdnum = r->rdi;

    struct process* current_process = this_cpu()->running_thread->process;

    if (fd_close(current_process, fdnum)) {
        r->rax = 0;
    } else {
        r->rax = -1;
    }
}

void syscall_read(struct registers* r) {
    int fdnum = r->rdi;
    void* buf = (void*) r->rsi;
    size_t count = r->rdx;

    struct file_descriptor* fd = fd_from_fdnum(this_cpu()->running_thread->process, fdnum);
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

    ssize_t read = node->read(node, buf, fd->offset, count);
    if (read < 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    fd->offset += read;
    r->rax = read;
}

void syscall_write(struct registers* r) {
    int fdnum = r->rdi;
    const void* buf = (const void*) r->rsi;
    size_t count = r->rdx;

    struct file_descriptor* fd = fd_from_fdnum(this_cpu()->running_thread->process, fdnum);
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

    ssize_t written = node->write(node, buf, fd->offset, count);
    if (written < 0) {
        r->rax = (uint64_t) -1;
        return;
    }

    fd->offset += written;
    r->rax = written;
}

void syscall_ioctl(struct registers* r) {
    int fdnum = r->rdi;
    uint64_t request = r->rsi;
    void* argp = (void*) r->rdx;

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
    r->rax = node->ioctl(node, request, argp);
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

    if (fd->node->type == VFS_NODE_REGULAR) {
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
            new_offset = offset + fd->node->size;
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

void syscall_chdir(struct registers* r) {
    const char* path = (char*) r->rdi;

    struct process* current_process = this_cpu()->running_thread->process;

    if (path == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= PATH_MAX) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if (node->type != VFS_NODE_DIRECTORY) {
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

    if (buffer == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    char temp_buffer[PATH_MAX];
    size_t actual_len = vfs_get_pathname(current_process->cwd, temp_buffer, sizeof(temp_buffer) - 1);

    if (actual_len >= len) {
        r->rax = (uint64_t) -1;
        return;
    }

    strncpy(buffer, temp_buffer, len);
    r->rax = 0;
}
