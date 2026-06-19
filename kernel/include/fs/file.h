#ifndef _KERNEL_FS_FILE_H
#define _KERNEL_FS_FILE_H

#include <fs/vfs.h>
#include <stddef.h>

#define AT_FDCWD -100

#define AT_EMPTY_PATH       (1 << 0)
#define AT_SYMLINK_NOFOLLOW (1 << 1)

#define O_PATH      01000

#define O_RDONLY    00000
#define O_WRONLY    00001
#define O_RDWR      00002
#define O_ACCMODE   (00003 | O_PATH)

#define O_CREAT     00004
#define O_DIRECTORY 00010
#define O_TRUNC     00020
#define O_APPEND    00040
#define O_EXCL      00100
#define O_NONBLOCK  00200
#define O_CLOEXEC   00400

struct process;

struct file {
    struct vfs_node* node;
    int flags;
    off_t offset;
    size_t refcount;
}; 

struct file_descriptor {
    struct file* file;
    bool cloexec;
};


int file_close(struct process* process, int fd);
struct file* file_create(struct vfs_node* node, int flags);
int file_dup(struct process* process, int old_fd, int new_fd, bool exact, bool cloexec);
void file_fork(struct process* old_process, struct process* new_process);
struct file* file_get(struct process* process, int fd);
int file_insert(struct process* process, struct file* file, bool cloexec);
void file_release(struct file* file);

void file_cleanup_dirfd(struct file* dirfile, struct vfs_node* dirnode);
int file_resolve_dirfd(struct process* process, int dirfd, const char* path, struct file** dirfile, struct vfs_node** dirnode);

void file_init(void);

#endif /* _KERNEL_FS_FILE_H */
