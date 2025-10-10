#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h> 
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_mkdir(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t path_len;
    if ((ret = user_strlen(path, &path_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* kpath = kmalloc(path_len + 1);
    if (unlikely(kpath == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    if ((ret = user_memcpy_from_user(kpath, path, path_len)) < 0) {
        kfree(kpath);
        r->rax = ret;
        return;
    }

    struct file* dirfile;
    struct vfs_node* dirnode;
    if ((ret = file_resolve_dirfd(current_process, dirfd, kpath, &dirfile, &dirnode)) < 0) {
        goto end;
    }

    ret = vfs_create(dirnode, kpath, VFS_TYPE_DIRECTORY, NULL);

end:
    r->rax = ret;

    if (dirnode != NULL) {
        VFS_NODE_UNREF(dirnode);
    }
    if (dirfile != NULL) {
        file_release(dirfile);
    }

    kfree(kpath);
}
