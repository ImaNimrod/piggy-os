#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/string.h>

// THREAD_FLAG_RETURN_SIGNAL_MASK, THREAD_FLAG_SHOULD_EXIT can just be set with __atomic functions
// while THREAD_FLAG_INTERRUPTABLE must be accessed while thread->state_lock is held because the
// interruptable state is intrinsically tied to thread state WAITING

extern void context_call_and_switch(void (*fn)(struct registers* r, void* arg), void* arg, void* stack);
[[noreturn]] extern void context_switch(struct registers* r);

static void internal_dequeue_unlocked(struct scheduler* sched, struct thread* thread);
static void internal_enqueue_unlocked(struct scheduler* sched, struct thread* thread);
[[noreturn]] static void reschedule(struct registers* r, void* arg);

static struct thread* get_next_thread(void) {
    spinlock_acquire(&this_cpu()->scheduler.run_queue_lock);

    struct thread* next = this_cpu()->scheduler.run_queue_head;
    if (next != NULL) {
        internal_dequeue_unlocked(&this_cpu()->scheduler, next);
    }

    spinlock_release(&this_cpu()->scheduler.run_queue_lock);

    if (next != NULL) {
        return next;
    }

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu_local* cpu = &cpu_local_data[i];
        if (cpu == this_cpu()) {
            continue;
        }

        struct scheduler* sched = &cpu->scheduler;

        spinlock_acquire(&sched->run_queue_lock);

        struct thread* stolen = sched->run_queue_tail;
        if (stolen != NULL) {
            internal_dequeue_unlocked(sched, stolen);
            stolen->cpu = this_cpu();
        }

        spinlock_release(&sched->run_queue_lock);

        if (stolen != NULL) {
            next = stolen;
            break;
        }
    }

    return next ? next : this_cpu()->scheduler.idle_thread;
}

static void idle(void) {
    sti();
    for (;;) {
        hlt();
    }
}

static void internal_dequeue_unlocked(struct scheduler* sched, struct thread* thread) {
    if (thread->prev != NULL) {
        thread->prev->next = thread->next;
    } else {
        sched->run_queue_head = thread->next;
    }

    if (thread->next != NULL) {
        thread->next->prev = thread->prev;
    } else {
        sched->run_queue_tail = thread->prev;
    }

    thread->prev = thread->next = NULL;
}

static void internal_enqueue_unlocked(struct scheduler* sched, struct thread* thread) {
    thread->next = NULL;
    thread->prev = sched->run_queue_tail;

    if (sched->run_queue_tail != NULL) {
        sched->run_queue_tail->next = thread;
    } else {
        sched->run_queue_head = thread;
    }

    sched->run_queue_tail = thread;
}

[[noreturn]] static void internal_thread_exit(struct registers* r, void* arg) {
    (void) arg;

    thread_destroy(this_cpu()->scheduler.current_thread);

    this_cpu()->scheduler.current_thread = NULL;
    reschedule(r, NULL);
}

[[noreturn]] static void internal_yield(struct registers* r, void* arg) {
    (void) arg;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    if (__atomic_load_n(&current_thread->flags, __ATOMIC_RELAXED) & THREAD_FLAG_SHOULD_EXIT) {
        internal_thread_exit(r, arg);
    }

    spinlock_acquire(&current_thread->state_lock);

    bool waiting = current_thread->state == THREAD_STATE_WAITING;
    bool interrupted = false;

    if (waiting && (current_thread->flags & THREAD_FLAG_INTERRUPTABLE)) {
        spinlock_acquire(&current_thread->signal_lock);

        if ((current_thread->pending_signals & ~current_thread->signal_mask) != 0) {
            current_thread->state = THREAD_STATE_READY;
            current_thread->flags &= ~THREAD_FLAG_INTERRUPTABLE;
            current_thread->wakeup_reason = -EINTR;
            interrupted = true;
        }

        spinlock_release(&current_thread->signal_lock);
    }

    spinlock_release(&current_thread->state_lock);

    if (!waiting || interrupted) {
        context_switch(r);
    } else {
        reschedule(r, NULL);
    }
}

[[noreturn]] static void reschedule(struct registers* r, void* arg)  {
    (void) arg;

    uint32_t remaining_ticks = lapic_timer_stop();

    if (this_cpu()->lapic_id == bsp_lapic_id) {
        timer_update_timers();
    }

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    // Save the current thread's context
    if (current_thread != this_cpu()->scheduler.idle_thread && current_thread != NULL) {
        memcpy64((void*) &current_thread->registers, (const void*) r, sizeof(struct registers) >> 3);

        if (current_thread->flags & THREAD_FLAG_USER) {
            this_cpu()->fpu_save(current_thread->fpu_context);
            current_thread->fs_base = this_cpu()->read_fs_base();
            current_thread->gs_base = rdmsr(MSR_IA32_KERNEL_GS_BASE);
        }

        // Thread / process time accounting
        uint32_t remaining_ms = remaining_ticks / this_cpu()->lapic_ticks_per_ms;
        struct timespec quanta_used = { 0, MS_TO_NS(SCHEDULER_TIME_QUANTA_MS - remaining_ms) };

        timespec_add(&current_thread->time_used, &quanta_used);
        timespec_add(&current_thread->process->time_used, &quanta_used);

        spinlock_acquire(&current_thread->state_lock);
        bool requeue = current_thread->state == THREAD_STATE_RUNNING;
        spinlock_release(&current_thread->state_lock);

        if (requeue) {
            scheduler_enqueue(&current_thread->cpu->scheduler, current_thread);
        }
    }

    // Switch to the next thread's context
    struct thread* next_thread = get_next_thread();

    if (__atomic_load_n(&next_thread->flags, __ATOMIC_RELAXED) & THREAD_FLAG_SHOULD_EXIT) {
        this_cpu()->scheduler.current_thread = next_thread;
        internal_thread_exit(r, NULL);
    }

    spinlock_acquire(&next_thread->state_lock);
    next_thread->state = THREAD_STATE_RUNNING;
    spinlock_release(&next_thread->state_lock);

    this_cpu()->scheduler.current_thread = next_thread;

    lapic_eoi();
    lapic_timer_oneshot(SCHEDULER_IRQ_VECTOR, SCHEDULER_TIME_QUANTA_MS);

    if (next_thread != this_cpu()->scheduler.idle_thread && (!current_thread || current_thread->process != next_thread->process)) {
        if (next_thread->flags & THREAD_FLAG_USER) {
            pagemap_load(next_thread->process->vmm_context->pagemap);
        } else {
            pagemap_load(kernel_pagemap);
        }
    }

    if (next_thread->flags & THREAD_FLAG_USER) {
        this_cpu()->fpu_restore(next_thread->fpu_context);
        this_cpu()->write_fs_base(next_thread->fs_base);
        wrmsr(MSR_IA32_KERNEL_GS_BASE, next_thread->gs_base);
    }

    this_cpu()->tss.rsp0 = next_thread->kernel_stack;
    context_switch(&next_thread->registers);
}

static void timer_callback(void* arg) {
    struct thread* thread = arg;
    scheduler_wakeup(thread, 0);
}

[[noreturn]] void scheduler_await(void) {
    this_cpu()->scheduler.current_thread = NULL;

    lapic_send_ipi(LAPIC_IPI_SELF, SCHEDULER_IRQ_VECTOR);
    sti();

    for (;;) {
        hlt();
    }
}

void scheduler_dequeue(struct scheduler* sched, struct thread* thread) {
    spinlock_acquire(&sched->run_queue_lock);
    internal_dequeue_unlocked(sched, thread);
    spinlock_release(&sched->run_queue_lock);
}

void scheduler_enqueue(struct scheduler* sched, struct thread* thread) {
    spinlock_acquire(&thread->state_lock);

    if (thread->state == THREAD_STATE_READY) {
        kpanic(NULL, true, "double thread enqueue");
    }

    thread->state = THREAD_STATE_READY;

    spinlock_acquire(&sched->run_queue_lock);
    internal_enqueue_unlocked(sched, thread);
    spinlock_release(&sched->run_queue_lock);

    spinlock_release(&thread->state_lock);
}

void scheduler_prepare_wait(struct thread* thread, bool interruptable) {
    spinlock_acquire(&thread->state_lock);

    thread->state = THREAD_STATE_WAITING;
    thread->flags |= (interruptable ? THREAD_FLAG_INTERRUPTABLE : 0);
    thread->wakeup_reason = 0;

    spinlock_release(&thread->state_lock);
}

int scheduler_sleep(struct thread* thread, const struct timespec* duration) {
    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);

    struct timer_event event;

    int ret = timer_setup(&event, timer_callback, thread, duration);
    if (ret < 0) {
        return ret;
    }

    ret = scheduler_yield();
    if (ret < 0) {
        timer_remove(&event);
    }

    return ret;
}

[[noreturn]] void scheduler_thread_exit(void) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    spinlock_acquire(&current_process->thread_list_lock);
    vector_remove_by_value(current_process->threads, &current_thread);
    bool last = vector_size(current_process->threads)  == 0;
    spinlock_release(&current_process->thread_list_lock);

    if (last) {
        process_zombify();
    }

    PROCESS_UNREF(current_process);

    cli();
    context_call_and_switch(internal_thread_exit, NULL, (void*) (this_cpu()->scheduler_stack + KERNEL_STACK_SIZE));
    __builtin_unreachable();
}

bool scheduler_wakeup(struct thread* thread, int wakeup_reason) {
    spinlock_acquire(&thread->state_lock);

    if (thread->state != THREAD_STATE_WAITING) {
        spinlock_release(&thread->state_lock);
        return false;
    }

    if (wakeup_reason != 0 && !(thread->flags & THREAD_FLAG_INTERRUPTABLE)) {
        spinlock_release(&thread->state_lock);
        return false;
    }

    thread->state = THREAD_STATE_READY;
    thread->flags &= ~THREAD_FLAG_INTERRUPTABLE;
    thread->wakeup_reason = wakeup_reason;

    struct scheduler* sched = &thread->cpu->scheduler;
    spinlock_acquire(&sched->run_queue_lock);
    internal_enqueue_unlocked(sched, thread);
    spinlock_release(&sched->run_queue_lock);

    spinlock_release(&thread->state_lock);

    if (thread->cpu != this_cpu() && thread->cpu->scheduler.current_thread != thread->cpu->scheduler.idle_thread) {
        lapic_send_ipi(thread->cpu->lapic_id, SCHEDULER_IRQ_VECTOR);
    }
    return true;
}

int scheduler_yield(void) {
    bool int_state = get_interrupt_state();

    cli();

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    spinlock_acquire(&current_thread->state_lock);
    bool waiting = current_thread->state == THREAD_STATE_WAITING;
    spinlock_release(&current_thread->state_lock);

    context_call_and_switch(internal_yield, NULL, (void*) (this_cpu()->scheduler_stack + KERNEL_STACK_SIZE));

    int ret = 0;
    if (waiting) {
        spinlock_acquire(&current_thread->state_lock);
        ret = current_thread->wakeup_reason;
        spinlock_release(&current_thread->state_lock);
    }

    if (int_state) {
        sti();
    }

    return ret;
}

void scheduler_init(void) {
    isr_register_handler(SCHEDULER_IRQ_VECTOR, reschedule, NULL);
    klog("[scheduler] initialized scheduler\n");
}

void scheduler_percpu_init(void) {
    this_cpu()->scheduler_stack = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB) + HIGH_VMA;
    this_cpu()->tss.ist1 = this_cpu()->scheduler_stack + KERNEL_STACK_SIZE;

    struct thread* idle_thread = thread_create_kernel((uintptr_t) idle, NULL);
    if (unlikely(!idle_thread)) {
        kpanic(NULL, false, "failed to create idle thread");
    }

    scheduler_enqueue(&idle_thread->cpu->scheduler, idle_thread);

    this_cpu()->scheduler.idle_thread = idle_thread;
    this_cpu()->scheduler.current_thread = idle_thread;
}
