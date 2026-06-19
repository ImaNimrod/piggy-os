#include <cpu/asm.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/pmm.h>
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/list.h>
#include <utils/usercopy.h>
#include <utils/vector.h>

enum {
    DEFAULT_ACTION_CONTINUE,
    DEFAULT_ACTION_IGNORE,
    DEFAULT_ACTION_STOP,
    DEFAULT_ACTION_TERMINATION,
};

struct signal_frame {
    uintptr_t restorer;
    sigset_t saved_mask;
    struct registers context;
    void* fpu_context;
    uint64_t signal;
};

[[noreturn]] extern void context_switch(struct registers* r);

// TODO: handle process stop / continue 
static int default_action(int signal) {
    switch (signal) {
        case SIGCONT:
            return DEFAULT_ACTION_CONTINUE;
        case SIGCHLD:
        case SIGWINCH:
        case SIGURG:
            return DEFAULT_ACTION_IGNORE;
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTOU:
            return DEFAULT_ACTION_STOP;
        default:
            return DEFAULT_ACTION_TERMINATION;
    }
}

void signal_handle_pending(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    for (;;) {
        spinlock_acquire(&current_thread->signal_lock);

        sigset_t old_mask = current_thread->signal_mask;
        sigset_t pending = current_thread->pending_signals & ~old_mask;
        if (pending == 0) {
            spinlock_release(&current_thread->signal_lock);
            return;
        }

        int signal = 63 - __builtin_clzll(pending) + 1;

        current_thread->pending_signals &= ~(1UL << (signal - 1));

        spinlock_acquire(&current_thread->state_lock);
        if (current_thread->flags & THREAD_FLAG_RETURN_SIGNAL_MASK) {
            old_mask = current_thread->return_signal_mask;
            current_thread->flags &= ~THREAD_FLAG_RETURN_SIGNAL_MASK;
        }
        spinlock_release(&current_thread->state_lock);

        spinlock_release(&current_thread->signal_lock);

        spinlock_acquire(&current_process->signal_actions_lock);
        struct sigaction action = current_process->signal_actions[signal - 1];
        spinlock_release(&current_process->signal_actions_lock);

        if (action.sa_handler == SIG_IGN) {
            continue;
        }

        if (action.sa_handler == SIG_DFL) {
            if (default_action(signal) == DEFAULT_ACTION_TERMINATION) {
                process_exit(current_process, PROCESS_EXITCODE(0, signal));
                scheduler_thread_exit();
            } else {
                continue;
            }
        }

        if (action.sa_restorer == NULL) {
            process_exit(current_process, PROCESS_EXITCODE(0, SIGSEGV));
            return;
        }

        spinlock_acquire(&current_thread->signal_lock);

        if (!(action.sa_flags & SA_NODEFER)) {
            current_thread->signal_mask |= (1UL << (signal - 1));
        }
        current_thread->signal_mask |= (action.sa_mask & ~UNBLOCKABLE_SIGNALS);

        spinlock_release(&current_thread->signal_lock);

        spinlock_acquire(&current_process->signal_actions_lock);
        if (action.sa_flags & SA_RESETHAND) {
            current_process->signal_actions[signal - 1].sa_handler = SIG_DFL;
        }
        spinlock_release(&current_process->signal_actions_lock);

        uintptr_t frame_sp;
        if ((action.sa_flags & SA_ONSTACK) && !(current_thread->signal_stack.ss_flags & SS_DISABLE) && !signal_on_altstack(current_thread, r->rsp)) {
            spinlock_acquire(&current_thread->signal_lock);
            frame_sp = (uintptr_t) current_thread->signal_stack.ss_sp + current_thread->signal_stack.ss_size;
            spinlock_release(&current_thread->signal_lock);
        } else {
            frame_sp = r->rsp;
        }

        struct signal_frame frame = {
            .saved_mask = old_mask,
            .context = *r,
            .fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA),
            .signal = signal,
            .restorer = (uintptr_t) action.sa_restorer,
        };

        this_cpu()->fpu_save(frame.fpu_context);

        void* user_frame_sp = (void*) ((frame_sp - 128 - sizeof(struct signal_frame)) & ~0xful);
        if (user_memcpy_to_user(user_frame_sp, &frame, sizeof(struct signal_frame)) < 0) {
            process_exit(current_thread->process, PROCESS_EXITCODE(0, SIGSEGV));
        }

        void* user_ret_sp = (void*) ((uintptr_t) user_frame_sp - 8);
        if (user_memcpy_to_user(user_ret_sp, &action.sa_restorer, 8) < 0) {
            process_exit(current_thread->process, PROCESS_EXITCODE(0, SIGSEGV));
        }

        r->rip = (uintptr_t) action.sa_handler;
        r->rsp = (uintptr_t) user_ret_sp;
        r->rdi = signal;

        context_switch(r);
        __builtin_unreachable();
    }
}

bool signal_on_altstack(struct thread* thread, uintptr_t sp) {
    spinlock_acquire(&thread->signal_lock);

    if (thread->signal_stack.ss_flags & SS_DISABLE) {
        spinlock_release(&thread->signal_lock);
        return false;
    }

    uintptr_t start = (uintptr_t) thread->signal_stack.ss_sp;
    uintptr_t end = start + thread->signal_stack.ss_size;

    spinlock_release(&thread->signal_lock);

    return sp >= start && sp < end;
}

[[noreturn]] void signal_restore_signal_frame(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    struct signal_frame frame;
    if (user_memcpy_from_user(&frame, (void*) r->rsp, sizeof(struct signal_frame)) < 0) {
        process_exit(current_thread->process, PROCESS_EXITCODE(0, SIGSEGV));
    }

    if (!IS_USER_ADDRESS((void*) frame.context.rip) || frame.context.cs != USER_CODE_SEGMENT || frame.context.ss != USER_DATA_SEGMENT) {
        process_exit(current_thread->process, PROCESS_EXITCODE(0, SIGSEGV));
    }

    frame.context.rflags &= ~(RFLAGS_TF | RFLAGS_DF | RFLAGS_RF);

    memcpy64((uint64_t*) r, (const uint64_t*) &frame.context, sizeof(struct registers) >> 3);
    this_cpu()->fpu_restore(frame.fpu_context);

    spinlock_acquire(&current_thread->signal_lock);
    current_thread->signal_mask = frame.saved_mask;
    spinlock_release(&current_thread->signal_lock);

    this_cpu()->tss.rsp0 = current_thread->kernel_stack;
    context_switch(r);
    __builtin_unreachable();
}

int signal_send_process(struct process* process, int signal) {
    spinlock_acquire(&process->thread_list_lock);

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        struct thread* thread = *vector_get(process->threads, i);

        spinlock_acquire(&thread->signal_lock);
        bool unblocked = signal == SIGKILL || signal == SIGSTOP || !(thread->signal_mask & (1UL << (signal - 1)));
        spinlock_release(&thread->signal_lock);

        if (unblocked) {
            int ret = signal_send_thread(thread, signal);
            spinlock_release(&process->thread_list_lock);
            return ret;
        }
    }

    spinlock_release(&process->thread_list_lock);

    // TODO: implement and correctly handle per process pending signal
    return -EAGAIN;
}

int signal_send_process_group(struct process_group* group, int signal) {
    int delivered = 0;
    int last_error = -ESRCH;

    mutex_acquire(&group->mutex);

    struct process* iter;
    DLIST_FOREACH(group->head, iter, group_next) {
        int ret = signal_send_process(iter, signal);
        if (ret < 0) {
            last_error = ret;
        } else {
            delivered++;
        }
    }

    mutex_release(&group->mutex);

    if (delivered > 0) {
        return 0;
    }

    return last_error;
}

int signal_send_thread(struct thread* thread, int signal) {
    struct process* process = thread->process;

    spinlock_acquire(&process->signal_actions_lock);

    if (signal != SIGKILL && signal != SIGSTOP) {
        struct sigaction* action = &process->signal_actions[signal - 1];
        if (action->sa_handler == SIG_IGN || (action->sa_handler == SIG_DFL && default_action(signal) == DEFAULT_ACTION_IGNORE)) {
            spinlock_release(&process->signal_actions_lock);
            return 0;
        }
    }

    spinlock_release(&process->signal_actions_lock);

    spinlock_acquire(&thread->signal_lock);
    thread->pending_signals |= (1UL << (signal - 1));
    spinlock_release(&thread->signal_lock);

    scheduler_wakeup(thread, THREAD_WAKEUP_REASON_INTERRUPTED);
    return 0;
}
