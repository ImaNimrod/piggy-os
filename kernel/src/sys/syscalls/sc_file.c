#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <types.h>
#include <utils/log.h>

// TODO: implement errno system

static struct file_descriptor* alloc_file_descriptor(struct vfs_node* node) {
    node->refcount++;

    struct file_descriptor* fd = kmalloc(sizeof(struct file_descriptor));
    if (fd == NULL) {
        node->refcount--;
        return NULL;
    }

    fd->node = node;
    fd->refcount = 1;
    fd->lock = (spinlock_t) {0};

    return fd;
}

static int alloc_fdnum(struct process* p, struct file_descriptor* fd) {
    spinlock_acquire(&p->fd_lock);

    for (int i = 0; i < MAX_FDS; i++) {
        if (p->file_descriptors[i] == NULL) {
            p->file_descriptors[i] = fd;
            spinlock_release(&p->fd_lock);
            return i;
        }
    }

    spinlock_release(&p->fd_lock);
    return -1;
}

static struct file_descriptor* fd_from_fdnum(struct process* p, int fdnum) {
    if (fdnum < 0 || fdnum > MAX_FDS) {
        return NULL;
    }

    spinlock_acquire(&p->fd_lock);

    struct file_descriptor* fd = p->file_descriptors[fdnum];
    if (fd != NULL) {
        fd->refcount++;
    }

    spinlock_release(&p->fd_lock);
    return fd;
}

void syscall_open(struct registers* r) {
    const char* path = (char*) r->rdi;
    int flags = r->rsi; // TODO: implement open flags

    struct process* current_process = this_cpu()->running_thread->process;

    struct vfs_node* node = vfs_get_node(current_process->cwd, path);
    if (node == NULL) {
        if ((flags & O_CREAT) != 0) {
            node = vfs_create(current_process->cwd, path, VFS_NODE_REGULAR);
        } else {
            r->rax = (uint64_t) -1;
            return;
        }
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

    struct file_descriptor* fd = alloc_file_descriptor(node);
    if (fd == NULL) {
        r->rax = (uint64_t) -1;
        return;
    }

    if ((flags & O_TRUNC) && node->type == VFS_NODE_REGULAR) {
        node->truncate(node, 0);
    }

    int fdnum = alloc_fdnum(current_process, fd);
    if (fdnum == -1) {
        kfree(fd);
        node->refcount--;
    }

    r->rax = fdnum;
}

void syscall_close(struct registers* r) {
    int fdnum = r->rdi;

    if (fdnum < 0 || fdnum >= MAX_FDS) {
        r->rax = (uint64_t) -1;
        return;
    }

    struct process* current_process = this_cpu()->running_thread->process;

    spinlock_acquire(&current_process->fd_lock);

    struct file_descriptor* fd = current_process->file_descriptors[fdnum];
    if (fd == NULL) {
        spinlock_release(&current_process->fd_lock);
        r->rax = (uint64_t) -1;
        return;
    }

    fd->node->refcount--;

    if (fd->refcount-- == 1) {
        kfree(fd);
    }

    current_process->file_descriptors[fdnum] = NULL;

    spinlock_release(&current_process->fd_lock);
    r->rax = 0;
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

    if (fd->node->type != VFS_NODE_REGULAR) {
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

    memcpy(buffer, temp_buffer, len);
    r->rax = 0;
}
