#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/list.h>
#include <utils/usercopy.h>

#define WNOHANG (1 << 0)

#define VALID_FLAGS (WNOHANG)

void sys_wait(struct registers* r) {
    pid_t pid = r->rdi;
    int* status = (int*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (flags & ~VALID_FLAGS) {
        r->rax = -EINVAL;
        return;
    }

    struct process* child = NULL;

    if (pid == -1) {
        for (;;) {
            if (!current_process->children) {
                r->rax = -ECHILD;
                return;
            }

            struct process* iter;
            SLIST_FOREACH(current_process->children, iter, sibling_next) {
                if (iter->state == PROCESS_STATE_ZOMBIE) {
                    child = iter;
                    goto end;
                }
            }

            if (flags & WNOHANG) {
                r->rax = -EAGAIN;
                return;
            }

            int ret = wait_queue_wait(&current_process->child_wq);
            if (ret < 0) {
                r->rax = ret;
                return;
            }
        }
    } else if (pid > 0) {
        struct process* iter;
        SLIST_FOREACH(current_process->children, iter, sibling_next) {
            if (iter->pid == pid) {
                child = iter;
                break;
            }
        }

        if (!child) {
            r->rax = -ECHILD;
            return;
        }

        if (child->state != PROCESS_STATE_ZOMBIE && (flags & WNOHANG)) {
            r->rax = -EAGAIN;
            return;
        }

        while (child->state != PROCESS_STATE_ZOMBIE) {
            int ret = wait_queue_wait(&current_process->child_wq);
            if (ret < 0) {
                r->rax = ret;
                return;
            }
        }
    } else {
        r->rax = -EINVAL;
        return;
    }

end:
    if (status) {
        int ret;
        if ((ret = user_memcpy_to_user(status, &child->exit_status, sizeof(int))) < 0) {
            r->rax = ret;
            return;
        }
    }

    r->rax = child->pid;

    process_destroy(child);
}
