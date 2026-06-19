#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_symlink(struct registers* r) {
    int link_dirfd = r->rdi;
    const char* link_path = (const char*) r->rsi;
    const char* target_path = (const char*) r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

    size_t link_path_len, target_path_len;

    if ((ret = user_strlen(link_path, &link_path_len)) < 0 || (ret = user_strlen(target_path, &target_path_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* klink_path = kmalloc(link_path_len + 1);
    if (unlikely(klink_path == NULL)) {
        r->rax = -ENOMEM;
        return;
    }
    klink_path[link_path_len] = '\0';

    char* ktarget_path = kmalloc(target_path_len + 1);
    if (unlikely(ktarget_path == NULL)) {
        kfree(klink_path);
        r->rax = -ENOMEM;
        return;
    }
    ktarget_path[target_path_len] = '\0';

    if ((ret = user_memcpy_from_user(klink_path, link_path, link_path_len)) < 0 || (ret = user_memcpy_from_user(ktarget_path, target_path, target_path_len)) < 0) {
        kfree(klink_path);
        kfree(ktarget_path);
        r->rax = ret;
        return;
    }

    struct file* link_dirfile = NULL;
    struct vfs_node* link_dirnode = NULL;

    if ((ret = file_resolve_dirfd(current_process, link_dirfd, klink_path, &link_dirfile, &link_dirnode)) < 0) {
        goto end;
    }

    ret = vfs_symlink(link_dirnode, klink_path, ktarget_path);
    file_cleanup_dirfd(link_dirfile, link_dirnode);

end:
    r->rax = ret;
}
