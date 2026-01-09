#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/vfs.h>
#include <mem/slab.h> 
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_unmount(struct registers* r) {
    const char* target = (const char*) r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t target_len;
    if ((ret = user_strlen(target, &target_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* ktarget = kmalloc(target_len + 1);
    if (unlikely(ktarget == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    struct vfs_node* reference = ktarget[0] == '/' ? process_get_root(current_process) : process_get_cwd(current_process);
    r->rax = vfs_unmount(reference, ktarget);
    VFS_NODE_UNREF(reference);

    kfree(ktarget);
}
