#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <mem/paging.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/string.h>

extern void context_switch(struct registers* r);
extern void context_call_and_switch(void (*fn)(struct registers* r, void* arg), void* arg, void* stack);

static void internal_dequeue_unlocked(struct scheduler* sched, struct thread* thread);
static void internal_enqueue_unlocked(struct scheduler* sched, struct thread* thread);

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

NORETURN static void reschedule(struct registers* r, void* arg)  {
    (void) arg;

    uint32_t remaining_ticks = lapic_timer_stop();

    if (this_cpu()->lapic_id == bsp_lapic_id) {
        timer_update_timers();
    }

    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    // Save the current thread's context
    if (current_thread != this_cpu()->scheduler.idle_thread && current_thread != NULL) {
        spinlock_release(&current_thread->yield_lock);

        memcpy64((void*) &current_thread->registers, (const void*) r, sizeof(struct registers) >> 3);

        if (current_thread->is_user) {
            this_cpu()->fpu_save(current_thread->fpu_context);
            current_thread->fs_base = rdmsr(MSR_IA32_FS_BASE);
            current_thread->gs_base = rdmsr(MSR_IA32_KERNEL_GS_BASE);
        }

        // Thread / process time accounting
        uint32_t remaining_ms = remaining_ticks / this_cpu()->lapic_ticks_per_ms;
        struct timespec quanta_used = { 0, MS_TO_NS(SCHEDULER_TIME_QUANTA_MS - remaining_ms) };

        timespec_add(&current_thread->time_used, &quanta_used);
        timespec_add(&current_thread->process->time_used, &quanta_used);

        spinlock_release(&current_thread->run_lock);

        spinlock_acquire(&current_thread->state_lock);
        bool requeue = current_thread->state == THREAD_RUNNING;
        spinlock_release(&current_thread->state_lock);

        if (requeue) {
            scheduler_enqueue(&current_thread->cpu->scheduler, current_thread);
        }
    }

    // Switch to the next thread's context
    struct thread* next_thread = get_next_thread();

    spinlock_acquire(&next_thread->state_lock);
    next_thread->state = THREAD_RUNNING;
    spinlock_release(&next_thread->state_lock);

    this_cpu()->scheduler.current_thread = next_thread;

    this_cpu()->tss.rsp0 = next_thread->kernel_stack;

    lapic_eoi();
    lapic_timer_oneshot(SCHEDULER_IRQ_VECTOR, SCHEDULER_TIME_QUANTA_MS);

    if (next_thread->is_user) {
        this_cpu()->fpu_restore(next_thread->fpu_context);
        wrmsr(MSR_IA32_FS_BASE, next_thread->fs_base);
        wrmsr(MSR_IA32_KERNEL_GS_BASE, next_thread->gs_base);
    }

    if (next_thread != this_cpu()->scheduler.idle_thread && (current_thread == NULL || current_thread->process != next_thread->process)) {
        if (next_thread->is_user) {
            pagemap_load(next_thread->process->vmm_context->pagemap);
        } else {
            pagemap_load(kernel_pagemap);
        }
    }

    if (next_thread->registers.cs & 0x03) {
        swapgs();
    }

    context_switch(&next_thread->registers);
    __builtin_unreachable();
}

NORETURN static void thread_exit_internal(struct registers* r, void* arg) {
    struct thread* current_thread = this_cpu()->scheduler.current_thread;

    vector_remove_by_value(current_thread->process->threads, &current_thread);
    thread_destroy(current_thread);

    this_cpu()->scheduler.current_thread = NULL;
    reschedule(r, arg);
    __builtin_unreachable();
}

void scheduler_block(struct thread* thread) {
    spinlock_acquire(&thread->state_lock);
    thread->state = THREAD_BLOCKED;
    spinlock_release(&thread->state_lock);

    scheduler_yield(true);
}

void scheduler_block_and_release(struct thread* thread, spinlock_t* lock, bool int_state) {
    spinlock_acquire(&thread->state_lock);
    thread->state = THREAD_BLOCKED;
    spinlock_release(&thread->state_lock);

    spinlock_release_irqsave(lock, int_state);

    scheduler_yield(true);
}

void scheduler_dequeue(struct scheduler* sched, struct thread* thread) {
    spinlock_acquire(&sched->run_queue_lock);
    internal_dequeue_unlocked(sched, thread);
    spinlock_release(&sched->run_queue_lock);
}

void scheduler_enqueue(struct scheduler* sched, struct thread* thread) {
    spinlock_acquire(&thread->state_lock);

    if (thread->state == THREAD_READY) {
        kpanic(NULL, true, "double thread enqueue");
    }

    thread->state = THREAD_READY;

    spinlock_acquire(&sched->run_queue_lock);
    internal_enqueue_unlocked(sched, thread);
    spinlock_release(&sched->run_queue_lock);

    spinlock_release(&thread->state_lock);
}

void scheduler_sleep(struct thread* thread, const struct timespec* duration) {
    timer_sleep_thread(thread, duration);
    scheduler_block(thread);
}

NORETURN void scheduler_thread_exit(void) {
    cli();
    context_call_and_switch(thread_exit_internal, NULL, (void*) (this_cpu()->scheduler_stack + KERNEL_STACK_SIZE));
    __builtin_unreachable();
}

void scheduler_unblock(struct thread* thread) {
    scheduler_enqueue(&thread->cpu->scheduler, thread);

    if (thread->cpu != this_cpu() && thread->cpu->scheduler.current_thread != thread->cpu->scheduler.idle_thread) {
        lapic_send_ipi(thread->cpu->lapic_id, SCHEDULER_IRQ_VECTOR);
    }
}

void scheduler_yield(bool save) {
    struct thread* thread = this_cpu()->scheduler.current_thread;

    if (save) {
        spinlock_acquire(&thread->yield_lock);
    } else {
        this_cpu()->scheduler.current_thread = NULL;
    }

    lapic_send_ipi(LAPIC_IPI_SELF, SCHEDULER_IRQ_VECTOR);
    sti();

    if (save) {
        spinlock_acquire(&thread->yield_lock);
        spinlock_release(&thread->yield_lock);
    } else {
        for (;;) {
            hlt();
        }
    }
}

void scheduler_init(void) {
    isr_register_handler(SCHEDULER_IRQ_VECTOR, reschedule, NULL);
    klog("[scheduler] initialized scheduler\n");
}
