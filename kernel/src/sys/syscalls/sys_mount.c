#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/macros.h> 
#include <utils/usercopy.h> 

void sys_mount(struct registers* r) {
    const char* source = (const char*) r->rdi;
    const char* target = (const char*) r->rsi;
    const char* fs_name = (const char*) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t target_len;
    if ((ret = user_strlen(target, &target_len)) < 0) {
        r->rax = ret;
        return;
    }

    size_t fs_name_len;
    if ((ret = user_strlen(fs_name, &fs_name_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* ktarget = kmalloc(target_len + 1);
    if (unlikely(ktarget == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    char* kfs_name = kmalloc(fs_name_len + 1);
    if (unlikely(kfs_name == NULL)) {
        kfree(ktarget);
        r->rax = -ENOMEM;
        return;
    }

    VFS_NODE_REF(current_process->cwd);

    struct vfs_node* backing_node = NULL;
    char* ksource = NULL;

    if (source != NULL) {
        size_t source_len;
        if ((ret = user_strlen(source, &source_len)) < 0) {
            goto end;
        }

        ksource = kmalloc(source_len + 1);
        if (unlikely(ksource == NULL)) {
            ret = -ENOMEM;
            goto end;
        }

        ret = vfs_lookup(current_process->cwd, ksource, false, NULL, &backing_node);
        if (ret < 0) {
            goto end;
        }

        backing_node->ops->unlock(backing_node);
    }

    ret = vfs_mount(backing_node, current_process->cwd, ktarget, kfs_name);

end:
    if (backing_node != NULL) {
        VFS_NODE_UNREF(backing_node);
        kfree(ksource);
    }

    VFS_NODE_UNREF(current_process->cwd);

    if (ksource != NULL) {
        kfree(ksource);
    }
    if (kfs_name != NULL) {
        kfree(kfs_name);
    }
    if (ktarget != NULL) {
        kfree(ktarget);
    }

    r->rax = ret;
}
