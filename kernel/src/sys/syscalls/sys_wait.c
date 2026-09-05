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


    bool check_pgid = false;

    if (pid < -1 || pid == 0) {
        check_pgid = true;
        pid = -pid;
    }

    mutex_acquire(&current_process->mutex);

    struct process* prev = NULL;
    struct process* iter = current_process->children;
    struct process* desired = NULL;

    if (!iter) {
        mutex_release(&current_process->mutex);
        r->rax = -ECHILD;
        return;
    }

    bool zombie = false;

    for (;;) {
        if (!iter) {
            if (pid > 0 && !desired) {
                mutex_release(&current_process->mutex);
                r->rax = -ECHILD;
                return;
            }

            if (flags & WNOHANG) {
                mutex_release(&current_process->mutex);
                r->rax = 0;
                return;
            }

            int ret = wait_queue_wait_mutex(&current_process->child_wq, &current_process->mutex, true);
            if (ret < 0) {
                mutex_release(&current_process->mutex);
                r->rax = ret;
                return;
            }

            prev = NULL;
            iter = current_process->children;
            desired = NULL;
            continue;
        }

        if ((pid > 0 && iter->pid == pid) || pid == -1 || (check_pgid && ((pid == 0 && current_process->group->pgid == iter->group->pgid) || iter->group->pgid == pid))) {
            desired = iter;
            zombie = iter->state == PROCESS_STATE_ZOMBIE;
        }

        if (zombie) {
            break;
        }

        prev = iter;
        iter = iter->sibling_next;
    }

    pid_t child_pid = iter->pid;

    if (status) {
        int ret = user_memcpy_to_user(status, &iter->exit_status, sizeof(*status));
        if (ret < 0) {
            mutex_release(&current_process->mutex);
            r->rax = ret;
            return;
        }
    }

    if (zombie) {
        if (prev) {
            prev->sibling_next = iter->sibling_next;
        } else {
            current_process->children = iter->sibling_next;
        }
    }

    mutex_release(&current_process->mutex);

    if (zombie) {
        PROCESS_UNREF(iter);
    }

    r->rax = child_pid;
}
