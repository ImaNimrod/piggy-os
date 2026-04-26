#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/lapic.h>
#include <cpu/smp.h>
#include <mem/paging.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

extern void context_switch(struct registers* r);
extern void context_call_and_switch(void (*fn)(struct registers* r, void* arg), void* arg, void* stack);

static struct thread* blocked_thread_list;
static spinlock_t running_thread_list_lock;

static struct thread* running_thread_list;
static spinlock_t blocked_thread_list_lock;

static struct thread* get_next_runnable_thread(struct thread* current_thread) {
    spinlock_acquire(&running_thread_list_lock);

    struct thread* iter;
    if (current_thread == NULL || current_thread == this_cpu()->idle_thread) {
        iter = running_thread_list;
    } else {
        iter = current_thread->next;
    }

    while (iter != NULL) {
        if (iter->state != THREAD_READY) {
            iter = iter->next;
            continue;
        }

        if (spinlock_test_and_acquire(&iter->run_lock)) {
            spinlock_release(&running_thread_list_lock);
            return iter;
        }

        iter = iter->next;
    }

    spinlock_release(&running_thread_list_lock);
    return NULL;
}

NORETURN static void reschedule(struct registers* r, void* arg)  {
    (void) arg;

    uint32_t remaining_ticks = lapic_timer_stop();

    if (this_cpu()->lapic_id == bsp_lapic_id) {
        timer_update_timers();
    }

    struct thread* current_thread = this_cpu()->running_thread;
    struct thread* next_thread = get_next_runnable_thread(current_thread);

    // Save the current thread's context
    if (current_thread != this_cpu()->idle_thread && current_thread != NULL) {
        spinlock_release(&current_thread->yield_lock);

        memcpy64((void*) &current_thread->registers, (const void*) r, sizeof(struct registers) >> 3);

        if (current_thread->is_user) {
            this_cpu()->fpu_save(current_thread->fpu_context);
            current_thread->fs_base = rdmsr(MSR_IA32_FS_BASE);
            current_thread->gs_base = rdmsr(MSR_IA32_KERNEL_GS_BASE);
        }

        if (current_thread->state == THREAD_RUNNING) {
            current_thread->state = THREAD_READY;
        }

        // Thread / process time accounting
        uint32_t remaining_ms = remaining_ticks / this_cpu()->lapic_ticks_per_ms;
        struct timespec quanta_used = { 0, MS_TO_NS(SCHEDULER_TIME_QUANTA_MS - remaining_ms) };

        timespec_add(&current_thread->time_used, &quanta_used);
        timespec_add(&current_thread->process->time_used, &quanta_used);

        spinlock_release(&current_thread->run_lock);
    }

    // If we find no threads to run, idle the cpu
    if (next_thread == NULL) {
        next_thread = this_cpu()->idle_thread;
    }

    // Switch to the next thread's context
    this_cpu()->running_thread = next_thread;
    next_thread->state = THREAD_RUNNING;

    this_cpu()->tss.rsp0 = next_thread->kernel_stack;

    lapic_eoi();
    lapic_timer_oneshot(SCHEDULER_IRQ_VECTOR, SCHEDULER_TIME_QUANTA_MS);

    if (next_thread->is_user) {
        this_cpu()->fpu_restore(next_thread->fpu_context);
        wrmsr(MSR_IA32_FS_BASE, next_thread->fs_base);
        wrmsr(MSR_IA32_KERNEL_GS_BASE, next_thread->gs_base);
    }

    if (next_thread != this_cpu()->idle_thread && (current_thread == NULL || current_thread->process != next_thread->process)) {
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
    struct thread* current_thread = this_cpu()->running_thread;

    scheduler_dequeue(current_thread);

    vector_remove_by_value(current_thread->process->threads, &current_thread);
    thread_destroy(current_thread);

    this_cpu()->running_thread = NULL;
    reschedule(r, arg);
    __builtin_unreachable();
}

void scheduler_block(struct thread* t) {
    spinlock_acquire(&running_thread_list_lock);
    SLIST_REMOVE(running_thread_list, t);
    spinlock_release(&running_thread_list_lock);

    spinlock_acquire(&blocked_thread_list_lock);
    t->state = THREAD_BLOCKED;
    SLIST_PUSH_BACK(blocked_thread_list, t);
    spinlock_release(&blocked_thread_list_lock);

    scheduler_yield(true);
}

void scheduler_block_and_release(struct thread* t, spinlock_t* lock, bool int_state) {
    spinlock_acquire(&running_thread_list_lock);
    SLIST_REMOVE(running_thread_list, t);
    spinlock_release(&running_thread_list_lock);

    spinlock_acquire(&blocked_thread_list_lock);
    t->state = THREAD_BLOCKED;
    SLIST_PUSH_BACK(blocked_thread_list, t);
    spinlock_release(&blocked_thread_list_lock);

    spinlock_release_irqsave(lock, int_state);

    scheduler_yield(true);
}

void scheduler_dequeue(struct thread* t) {
    spinlock_acquire(&running_thread_list_lock);
    SLIST_REMOVE(running_thread_list, t);
    spinlock_release(&running_thread_list_lock);
}

void scheduler_enqueue(struct thread* t) {
    spinlock_acquire(&running_thread_list_lock);
    SLIST_PUSH_BACK(running_thread_list, t);
    spinlock_release(&running_thread_list_lock);
}

void scheduler_sleep(struct thread* t, const struct timespec* tp) {
    timer_sleep_thread(t, tp);
    scheduler_block(t);
}

NORETURN void scheduler_thread_exit(void) {
    cli();
    context_call_and_switch(thread_exit_internal, NULL, (void*) (this_cpu()->scheduler_stack + KERNEL_STACK_SIZE));
    __builtin_unreachable();
}

void scheduler_unblock(struct thread* t) {
    spinlock_acquire(&blocked_thread_list_lock);
    SLIST_REMOVE(blocked_thread_list, t);
    spinlock_release(&blocked_thread_list_lock);

    spinlock_acquire(&running_thread_list_lock);
    if (t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY;
    }
    SLIST_PUSH_BACK(running_thread_list, t);
    spinlock_release(&running_thread_list_lock);
}

void scheduler_yield(bool save) {
    struct thread* thread = this_cpu()->running_thread;

    if (save) {
        spinlock_acquire(&thread->yield_lock);
    } else {
        this_cpu()->running_thread = NULL;
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
