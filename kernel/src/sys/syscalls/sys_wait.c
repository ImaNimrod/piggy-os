#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/list.h>
#include <utils/usercopy.h>

#define WNOHANG (1 << 0)

void sys_wait(struct registers* r) {
    pid_t pid = r->rdi;
    int* status = (int*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct process* child = NULL;

    if (pid == -1) {
        for (;;) {
            if (current_process->children == NULL) {
                r->rax = -ECHILD;
                return;
            }

            struct process* iter;
            SLIST_FOREACH(current_process->children, iter) {
                if (iter->state == PROCESS_ZOMBIE) {
                    child = iter;
                    goto end;
                }
            }

            if (flags & WNOHANG) {
                r->rax = -EAGAIN;
                return;
            }

            scheduler_yield(true);
        }
    } else if (pid > 0) {
        struct process* iter;
        SLIST_FOREACH(current_process->children, iter) {
            if (iter->pid == pid) {
                child = iter;
                break;
            }
        }

        if (child == NULL) {
            r->rax = -ECHILD;
            return;
        }

        if (child->state != PROCESS_ZOMBIE && (flags & WNOHANG)) {
            r->rax = -EAGAIN;
            return;
        }

        while (child->state != PROCESS_ZOMBIE) {
            scheduler_yield(true);
        }
    } else {
        r->rax = -EINVAL;
        return;
    }

end:
    if (status != NULL) {
        int ret;
        if ((ret = user_memcpy_to_user(status, &child->exit_status, sizeof(int*))) < 0) {
            r->rax = ret;
            return;
        }
    }

    r->rax = child->pid;
}
