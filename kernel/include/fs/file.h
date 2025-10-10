#ifndef _KERNEL_FS_FILE_H
#define _KERNEL_FS_FILE_H 1

#include <fs/vfs.h>
#include <sys/process.h>
#include <types.h>

struct process;

struct file {
    struct vfs_node* node;
    int flags;
    off_t offset;
    size_t refcount;
}; 

struct file* file_create(struct vfs_node* node, int flags);
void file_fork(struct process* old_process, struct process* new_process);
struct file* file_get(struct process* process, int fd);
int file_insert(struct process* process, struct file* file);
void file_release(struct file* file);
int file_resolve_dirfd(struct process* process, int dirfd, const char* path, struct file** dirfile, struct vfs_node** dirnode);

#endif /* _KERNEL_FS_FILE_H */
