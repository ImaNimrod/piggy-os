#ifndef _KERNEL_FS_FD_H
#define _KERNEL_FS_FD_H

#include <fs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>
#include <types.h>
#include <utils/spinlock.h>

#define FD_FLAGS_MASK (O_CLOEXEC)
#define FILE_CREATION_FLAGS_MASK (O_ACCMODE | O_CREAT | O_DIRECTORY | O_TRUNC | O_APPEND | O_EXCL | O_NONBLOCK)
#define FILE_FLAGS_MASK (FILE_CREATION_FLAGS_MASK | FD_FLAGS_MASK)

struct process;

struct file_descriptor {
    struct vfs_node* node;
    int flags;
    size_t refcount;
    off_t offset;
    spinlock_t lock;
};

struct file_descriptor* fd_create(struct vfs_node* node, int flags);
int fd_alloc_fdnum(struct process* p, struct file_descriptor* fd);
int fd_close(struct process* p, int fdnum);
int fd_dup(struct process* old_process, int old_fdnum, struct process* new_process, int new_fdnum, bool exact, bool cloexec);
struct file_descriptor* fd_from_fdnum(struct process* p, int fdnum);

#endif /* _KERNEL_FS_FD_H */
