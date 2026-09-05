#include <cpu/asm.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/pmm.h>
#include <sys/process.h>
#include <sys/signal.h>
#include <utils/list.h>
#include <utils/usercopy.h>
#include <utils/vector.h>

#include <utils/log.h>

enum {
    DEFAULT_ACTION_CONTINUE,
    DEFAULT_ACTION_IGNORE,
    DEFAULT_ACTION_STOP,
    DEFAULT_ACTION_TERMINATE,
};

struct signal_frame {
    uintptr_t restorer;
    sigset_t saved_mask;
    struct registers context;
    uint64_t fs_base;
    uint64_t gs_base;
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
            return DEFAULT_ACTION_TERMINATE;
    }
}

#include <utils/log.h>

void signal_handle_pending(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    for (;;) {
        bool int_status = spinlock_acquire_irqsave(&current_thread->signal_lock);

        sigset_t old_mask = current_thread->signal_mask;
        sigset_t pending = current_thread->pending_signals & ~old_mask;
        if (pending == 0) {
            spinlock_release_irqsave(&current_thread->signal_lock, int_status);
            return;
        }

        int signal = __builtin_ctzll(pending) + 1;

        current_thread->pending_signals &= ~(1ULL << (signal - 1));

        if (__atomic_fetch_and(&current_thread->flags, ~THREAD_FLAG_RETURN_SIGNAL_MASK, __ATOMIC_ACQUIRE) & THREAD_FLAG_RETURN_SIGNAL_MASK) {
            old_mask = current_thread->return_signal_mask;
        }

        spinlock_release_irqsave(&current_thread->signal_lock, int_status);

        int_status = spinlock_acquire_irqsave(&current_process->signal_actions_lock);
        struct sigaction action = current_process->signal_actions[signal - 1];
        spinlock_release_irqsave(&current_process->signal_actions_lock, int_status);

        if (action.sa_handler == SIG_IGN) {
            continue;
        }

        if (action.sa_handler == SIG_DFL) {
            if (default_action(signal) == DEFAULT_ACTION_TERMINATE) {
                process_exit(PROCESS_EXITCODE(0, signal));
            } else {
                continue;
            }
        }

        if (!action.sa_restorer) {
            process_exit(PROCESS_EXITCODE(0, SIGSEGV));
        }

        int_status = spinlock_acquire_irqsave(&current_thread->signal_lock);

        if (!(action.sa_flags & SA_NODEFER)) {
            current_thread->signal_mask |= (1ULL << (signal - 1));
        }
        current_thread->signal_mask |= (action.sa_mask & ~UNBLOCKABLE_SIGNALS);

        spinlock_release_irqsave(&current_thread->signal_lock, int_status);

        int_status = spinlock_acquire_irqsave(&current_process->signal_actions_lock);
        if (action.sa_flags & SA_RESETHAND) {
            current_process->signal_actions[signal - 1].sa_handler = SIG_DFL;
        }
        spinlock_release_irqsave(&current_process->signal_actions_lock, int_status);

        uintptr_t frame_sp;
        if ((action.sa_flags & SA_ONSTACK) && !(current_thread->signal_stack.ss_flags & SS_DISABLE) && !signal_on_altstack(current_thread, r->rsp)) {
            int_status = spinlock_acquire_irqsave(&current_thread->signal_lock);
            frame_sp = (uintptr_t) current_thread->signal_stack.ss_sp + current_thread->signal_stack.ss_size;
            spinlock_release_irqsave(&current_thread->signal_lock, int_status);
        } else {
            frame_sp = r->rsp - 128;
        }

        struct signal_frame frame = {
            .saved_mask = old_mask,
            .context = *r,
            .fs_base = this_cpu()->read_fs_base(),
            .gs_base = rdmsr(MSR_IA32_KERNEL_GS_BASE),
            .signal = signal,
            .restorer = (uintptr_t) action.sa_restorer,
        };

        frame.fpu_context = (void*) (pmm_alloc(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
        this_cpu()->fpu_save(frame.fpu_context);

        uintptr_t sp = frame_sp & ~0xfULL;
        sp -= sizeof(struct signal_frame);

        if (user_memcpy_to_user((void*) sp, &frame, sizeof(struct signal_frame)) < 0) {
            process_exit(PROCESS_EXITCODE(0, SIGSEGV));
        }

        sp -= sizeof(uintptr_t);

        if (user_memcpy_to_user((void*) sp, &action.sa_restorer, sizeof(uintptr_t)) < 0) {
            process_exit(PROCESS_EXITCODE(0, SIGSEGV));
        }

        r->rip = (uintptr_t) action.sa_handler;
        r->rsp = sp;
        r->rdi = signal;

        context_switch(r);
        __builtin_unreachable();
    }
}

bool signal_on_altstack(struct thread* thread, uintptr_t sp) {
    bool int_status = spinlock_acquire_irqsave(&thread->signal_lock);

    if (thread->signal_stack.ss_flags & SS_DISABLE) {
        spinlock_release_irqsave(&thread->signal_lock, int_status);
        return false;
    }

    uintptr_t start = (uintptr_t) thread->signal_stack.ss_sp;
    uintptr_t end = start + thread->signal_stack.ss_size;

    spinlock_release_irqsave(&thread->signal_lock, int_status);

    return sp >= start && sp < end;
}

[[noreturn]] void signal_restore_signal_frame(struct registers* r) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    struct signal_frame frame;
    if (user_memcpy_from_user(&frame, (void*) r->rsp, sizeof(struct signal_frame)) < 0) {
        process_exit(PROCESS_EXITCODE(0, SIGSEGV));
    }

    if (!IS_USER_ADDRESS((void*) frame.context.rip) || frame.context.cs != USER_CODE_SEGMENT || frame.context.ss != USER_DATA_SEGMENT) {
        process_exit(PROCESS_EXITCODE(0, SIGSEGV));
    }

    frame.context.rflags &= ~(RFLAGS_TF | RFLAGS_DF | RFLAGS_RF);

    *r = frame.context;

    this_cpu()->fpu_restore(frame.fpu_context);
    pmm_free((uintptr_t) frame.fpu_context - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB));

    this_cpu()->write_fs_base(frame.fs_base);
    wrmsr(MSR_IA32_KERNEL_GS_BASE, frame.gs_base);

    bool int_status = spinlock_acquire_irqsave(&current_thread->signal_lock);
    current_thread->signal_mask = frame.saved_mask;
    spinlock_release_irqsave(&current_thread->signal_lock, int_status);

    this_cpu()->tss.rsp0 = current_thread->kernel_stack;
    context_switch(r);
    __builtin_unreachable();
}

int signal_send_process(struct process* process, int signal) {
    spinlock_acquire(&process->thread_list_lock);

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        struct thread* thread = *vector_get(process->threads, i);

        bool int_status = spinlock_acquire_irqsave(&thread->signal_lock);
        bool unblocked = signal == SIGKILL || signal == SIGSTOP || !(thread->signal_mask & (1ULL << (signal - 1)));
        spinlock_release_irqsave(&thread->signal_lock, int_status);

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

    bool int_status = spinlock_acquire_irqsave(&process->signal_actions_lock);

    if (signal != SIGKILL && signal != SIGSTOP) {
        struct sigaction* action = &process->signal_actions[signal - 1];
        if (action->sa_handler == SIG_IGN || (action->sa_handler == SIG_DFL && default_action(signal) == DEFAULT_ACTION_IGNORE)) {
            spinlock_release_irqsave(&process->signal_actions_lock, int_status);
            return 0;
        }
    }

    spinlock_release_irqsave(&process->signal_actions_lock, int_status);

    int_status = spinlock_acquire_irqsave(&thread->signal_lock);
    thread->pending_signals |= (1ULL << (signal - 1));
    spinlock_release_irqsave(&thread->signal_lock, int_status);

    scheduler_wakeup(thread, -EINTR);
    return 0;
}
