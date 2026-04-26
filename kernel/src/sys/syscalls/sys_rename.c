#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_rename(struct registers* r) {
    int old_dirfd = r->rdi;
    const char* old_path = (const char*) r->rsi;
    int new_dirfd = r->rdx;
    const char* new_path = (const char*) r->r10;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

    size_t old_len, new_len;

    if ((ret = user_strlen(old_path, &old_len)) < 0 || (ret = user_strlen(new_path, &new_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* kold_path = kmalloc(old_len + 1);
    if (unlikely(kold_path == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    char* knew_path = kmalloc(new_len + 1);
    if (unlikely(knew_path == NULL)) {
        kfree(knew_path);
        r->rax = -ENOMEM;
        return;
    }

    if ((ret = user_memcpy_from_user(kold_path, old_path, old_len)) < 0 || (ret = user_memcpy_from_user(knew_path, new_path, new_len)) < 0) {
        kfree(kold_path);
        kfree(knew_path);
        r->rax = ret;
        return;
    }

    struct file* old_dirfile = NULL;
    struct vfs_node* old_dirnode = NULL;

    struct file* new_dirfile = NULL;
    struct vfs_node* new_dirnode = NULL;

    if ((ret = file_resolve_dirfd(current_process, old_dirfd, kold_path, &old_dirfile, &old_dirnode)) < 0) {
        goto end;
    }
    if ((ret = file_resolve_dirfd(current_process, new_dirfd, knew_path, &new_dirfile, &new_dirnode)) < 0) {
        goto end;
    }

    ret = vfs_rename(old_dirnode, kold_path, new_dirnode, knew_path);

end:
    if (old_dirnode != NULL) {
        file_cleanup_dirfd(old_dirfile, old_dirnode);
    }

    if (new_dirnode != NULL) {
        file_cleanup_dirfd(new_dirfile, new_dirnode);
    }

    r->rax = ret;
}
