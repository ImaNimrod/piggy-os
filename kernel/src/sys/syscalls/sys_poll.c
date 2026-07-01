#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <fs/poll.h>
#include <mem/slab.h>
#include <sys/process.h> 
#include <sys/timer.h> 
#include <types.h>
#include <utils/macros.h> 
#include <utils/usercopy.h> 
#include <utils/vector.h>

void sys_poll(struct registers* r) {
    struct pollfd* fds = (struct pollfd*) r->rdi;
    nfds_t nfds = r->rsi;
    const struct timespec* timeout = (const struct timespec*) r->rdx;
    const sigset_t* sigmask = (const sigset_t*) r->r10;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    struct pollfd* kfds = kmalloc(sizeof(struct pollfd) * nfds);
    if (unlikely(!kfds)) {
        r->rax = -ENOMEM;
        return;
    }

    int ret;
    if ((ret = user_memcpy_from_user(kfds, fds, sizeof(struct pollfd) * nfds)) < 0) {
        kfree(kfds);
        r->rax = ret;
        return;
    }

    struct timespec ktimeout;
    if (timeout != NULL) {
        if ((ret = user_memcpy_from_user(&ktimeout, timeout, sizeof(struct timespec))) < 0) {
            kfree(kfds);
            r->rax = ret;
            return;
        }
    }

    spinlock_acquire(&current_thread->signal_lock);
    sigset_t old_sigmask = current_thread->signal_mask;
    spinlock_release(&current_thread->signal_lock);

    if (sigmask != NULL) {
        sigset_t ksigmask;
        if ((ret = user_memcpy_from_user(&ksigmask, sigmask, sizeof(sigset_t))) < 0) {
            kfree(kfds);
            r->rax = ret;
            return;
        }

        spinlock_acquire(&current_thread->signal_lock);
        current_thread->signal_mask = ksigmask;
        spinlock_release(&current_thread->signal_lock);
    }

    struct poll_table pt;
    if ((ret = poll_table_init(&pt)) < 0) {
        goto end;
    }

    for (;;) {
        int ready = 0;

        for (nfds_t i = 0; i < nfds; i++) {
            struct file* file = file_get(current_process, kfds[i].fd);
            if (file == NULL) {
                kfds[i].revents = POLLNVAL;
                ready++;
                continue;
            }

            struct vfs_node* node = file->node;

            node->ops->lock(node);
            kfds[i].revents = node->ops->poll(node, kfds[i].events, &pt);
            node->ops->unlock(node);

            if (kfds[i].revents) {
                ready++;
            }
        }

        if (ready > 0) {
            break;
        }

        if ((ret = poll_table_wait(&pt, &ktimeout)) < 0) {
            if (ret == -ETIMEDOUT) {
                ret = 0;
            }
            goto end;
        }

        if ((ret = poll_table_reset(&pt)) < 0) {
            goto end;
        }
    }

    ret = user_memcpy_to_user(fds, kfds, sizeof(struct pollfd) * nfds);

end:
    r->rax = ret;

    poll_table_deinit(&pt);

    if (sigmask != NULL) {
        spinlock_acquire(&current_thread->signal_lock);
        current_thread->signal_mask = old_sigmask;
        spinlock_release(&current_thread->signal_lock);
    }

    kfree(kfds);
    return;
}
