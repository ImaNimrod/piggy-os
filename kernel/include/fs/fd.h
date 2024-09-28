#ifndef _KERNEL_FS_FD_H
#define _KERNEL_FS_FD_H

#include <fs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/process.h>
#include <types.h>
#include <utils/spinlock.h>

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
