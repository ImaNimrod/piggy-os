#include <cpu/isr.h>
#include <errno.h>
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/string.h>
#include <utils/usercopy.h>

#define PROCCTL_NEXTPID 1
#define PROCCTL_PREVPID 2
#define PROCCTL_STATUS  3
#define PROCCTL_CMDLINE 4
#define PROCCTL_VMMAPS  5

#define PROCCTL_PID_END ((pid_t) -1)

#define PROCCTL_STATE_RUNNING   0
#define PROCCTL_STATE_ZOMBIE    1

struct procctl_status {
    char name[PROCESS_NAME_MAX];
    int state;
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    size_t thread_count;
};

struct procctl_vm_map {
    uint64_t start;
    uint64_t end;
    int flags;
    int prot;
};

static inline int process_state_to_procctl_state(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_RUNNING:
            return PROCCTL_STATE_RUNNING;
        case PROCESS_STATE_ZOMBIE:
            return PROCCTL_STATE_ZOMBIE;
        default:
            __builtin_unreachable();
    }
}

void sys_procctl(struct registers* r) {
    pid_t pid = r->rdi;
    int op = r->rsi;
    void* buf = (void*) r->rdx;
    size_t max_len = r->r10;

    ssize_t ret = 0;

    switch (op) {
        case PROCCTL_NEXTPID:
            pid_t next = process_next_pid(pid);
            if (next < 0) {
                ret = next;
                break;
            }

            if (next == 0) {
                next = PROCCTL_PID_END;
            }

            ret = user_memcpy_to_user(buf, &next, sizeof(next));
            if (ret < 0) {
                break;
            }

            ret = sizeof(next);
            break;
        case PROCCTL_PREVPID:
            pid_t prev = process_prev_pid(pid);
            if (prev <= 0) {
                ret = prev;
                break;
            }

            if (prev == 0) {
                prev = PROCCTL_PID_END;
            }

            ret = user_memcpy_to_user(buf, &prev, sizeof(prev));
            if (ret < 0) {
                break;
            }

            ret = sizeof(prev);
            break;
        case PROCCTL_STATUS:
        case PROCCTL_CMDLINE:
        case PROCCTL_VMMAPS:
            struct process* process = process_find(pid);
            if (!process) {
                ret = -ESRCH;
                break;
            }

            size_t actual_len = 0;

            switch (op) {
                case PROCCTL_STATUS:
                    struct procctl_status status = {
                        .state = process_state_to_procctl_state(process->state),
                        .pid = process->pid,
                        .ppid = process->parent ? process->parent->pid : 0,
                        .pgid = process->group->pgid,
                        .thread_count = vector_size(process->threads),
                    };

                    strncpy(status.name, process->name, PROCESS_NAME_MAX);
                    status.name[PROCESS_NAME_MAX - 1] = '\0';

                    actual_len = MIN(max_len, sizeof(status));

                    ret = user_memcpy_to_user(buf, &status, actual_len);
                    if (ret < 0) {
                        break;
                    }

                    break;
                case PROCCTL_CMDLINE:
                    char** cmdline = process->cmdline;

                    for (size_t i = 0; cmdline[i]; i++) {
                        size_t arg_len = strlen(cmdline[i]);

                        if (SIZE_MAX - actual_len < arg_len + 1) {
                            ret = -EOVERFLOW;
                            break;
                        }

                        actual_len += arg_len + 1;
                    }

                    if (ret < 0) {
                        break;
                    }

                    size_t copy_len = MIN(max_len, actual_len);
                    size_t copied = 0;

                    for (size_t i = 0; cmdline[i] && copied < copy_len; i++) {
                        size_t arg_len = strlen(cmdline[i]) + 1;
                        size_t remaining = copy_len - copied;
                        size_t n = MIN(arg_len, remaining);

                        ret = user_memcpy_to_user((char*) buf + copied, cmdline[i], n);
                        if (ret < 0) {
                            break;
                        }

                        copied += n;
                    }

                    break;
                case PROCCTL_VMMAPS:
                    struct vmm_context* vmm_context = process->vmm_context;

                    mutex_acquire(&vmm_context->mutex);

                    size_t count = 0;
                    for (struct vmm_range* range = vmm_context->ranges; range; range = range->next) {
                        count++;
                    }

                    if (__builtin_mul_overflow(count, sizeof(struct procctl_vm_map), &actual_len)) {
                        mutex_release(&vmm_context->mutex);
                        ret = -EOVERFLOW;
                        break;
                    }

                    size_t max_count = max_len / sizeof(struct procctl_vm_map);
                    size_t copy_count = MIN(count, max_count);

                    struct vmm_range* range = vmm_context->ranges;

                    for (size_t i = 0; i < copy_count; i++, range = range->next) {
                        struct procctl_vm_map map = {
                            .start = range->base,
                            .end = range->base + range->size,
                            .flags = range->flags,
                            .prot = range->prot,
                        };

                        ret = user_memcpy_to_user((char*) buf + i * sizeof(map), &map, sizeof(map));
                        if (ret < 0) {
                            break;
                        }
                    }

                    mutex_release(&vmm_context->mutex);
                    break;
            }

            ret = (ssize_t) actual_len;
            break;
        default:
            ret = -EINVAL;
            break;
    }

    r->rax = ret;
}
