#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/usercopy.h>

void sys_chroot(struct registers* r) {
    const char* path = (const char*) r->rdi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

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

    struct vfs_node* reference = kpath[0] == '/' ? process_get_root(current_process) : process_get_cwd(current_process);

    struct vfs_node* new_root = NULL;
    if ((ret = vfs_lookup(reference, kpath, false, NULL, &new_root)) < 0) {
        goto end;
    }
    new_root->ops->unlock(new_root);

    if (new_root->type != VFS_TYPE_DIRECTORY) {
        ret = -ENOTDIR;
        goto end;
    }

    process_set_root(current_process, new_root);

end:
    if (new_root != NULL) {
        VFS_NODE_UNREF(new_root);
    }

    VFS_NODE_UNREF(reference);
    kfree(kpath);

    r->rax = ret;
}
