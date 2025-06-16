#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <dev/lapic.h>
#include <mem/vmm.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define SCHEDULER_IRQ_VECTOR 48 
#define SCHEDULER_TIME_QUANTA 5000

static struct thread* thread_list = NULL;
static spinlock_t thread_state_lock = {0};

static struct thread* get_next_runnable_thread(struct thread* current_thread) {
    spinlock_acquire(&thread_state_lock);

    struct thread* iter;
    if (current_thread == NULL || current_thread == this_cpu()->idle_thread) {
        iter = thread_list;
    } else {
        iter = current_thread->next;
    }

    while (iter != NULL) {
        if (iter->state != THREAD_READY) {
            iter = iter->next;
            continue;
        }

        if (spinlock_test_and_acquire(&iter->run_lock)) {
            spinlock_release(&thread_state_lock);
            return iter;
        }

        iter = iter->next;
    }

    spinlock_release(&thread_state_lock);
    return NULL;
}

NORETURN static void reschedule(struct registers* r, void* arg)  {
    (void) arg;

    lapic_timer_stop();

    struct thread* current_thread = this_cpu()->running_thread;
    struct thread* next_thread = get_next_runnable_thread(current_thread);

    /* save the current thread's context */
    if (current_thread != this_cpu()->idle_thread) {
        spinlock_release(&current_thread->yield_lock);

        memcpy(&current_thread->registers, r, sizeof(struct registers));

        if (current_thread->is_user) {
            this_cpu()->fpu_save(current_thread->fpu_context);
            current_thread->fs_base = this_cpu()->read_fs_base();
            current_thread->gs_base = rdmsr(IA32_KERNEL_GS_BASE_MSR);
        }

        if (current_thread->state == THREAD_RUNNING) {
            current_thread->state = THREAD_READY;
        }

        spinlock_release(&current_thread->run_lock);
    }

    /* if we find no threads to run, idle the cpu */
    if (next_thread == NULL) {
        next_thread = this_cpu()->idle_thread;
    }

    /* switch to the next thread's context */
    this_cpu()->running_thread = next_thread;
    next_thread->state = THREAD_RUNNING;

    this_cpu()->tss.rsp0 = next_thread->kernel_stack;
    this_cpu()->kernel_stack = next_thread->kernel_stack;

    lapic_eoi();
    lapic_timer_oneshot(SCHEDULER_IRQ_VECTOR, SCHEDULER_TIME_QUANTA);

    if (next_thread->is_user) {
        this_cpu()->fpu_restore(next_thread->fpu_context);
        this_cpu()->write_fs_base(next_thread->fs_base);
        wrmsr(IA32_KERNEL_GS_BASE_MSR, next_thread->gs_base);
    }

    if (next_thread != this_cpu()->idle_thread && (current_thread == NULL || current_thread->process != next_thread->process)) {
        vmm_switch_pagemap(next_thread->process->pagemap);
    }

    if (r->cs & 0x03) {
        swapgs();
    }

    asm volatile(
        "mov %0, %%rsp\n\t"
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%r11\n\t"
        "pop %%r10\n\t"
        "pop %%r9\n\t"
        "pop %%r8\n\t"
        "pop %%rsi\n\t"
        "pop %%rdi\n\t"
        "pop %%rbp\n\t"
        "pop %%rdx\n\t"
        "pop %%rcx\n\t"
        "pop %%rbx\n\t"
        "pop %%rax\n\t"
        "addq $16, %%rsp\n\t"
        "iretq\n\t"
        :: "r" (&next_thread->registers)
    );
    __builtin_unreachable();
}

NORETURN void scheduler_await(void) {
    cli();
    lapic_timer_oneshot(SCHEDULER_IRQ_VECTOR, SCHEDULER_TIME_QUANTA);
    sti();
    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

void scheduler_yield(void) {
    cli();
    lapic_timer_stop();
    sti();

    struct thread* thread = this_cpu()->running_thread;
    spinlock_acquire(&thread->yield_lock);

    lapic_send_ipi(LAPIC_IPI_SELF, SCHEDULER_IRQ_VECTOR);

    spinlock_acquire(&thread->yield_lock);
    spinlock_release(&thread->yield_lock);
}

void scheduler_thread_enqueue(struct thread* t) {
    spinlock_acquire(&thread_state_lock);
    SLIST_PUSH_BACK(thread_list, t);
    spinlock_release(&thread_state_lock);
}

void scheduler_thread_dequeue(struct thread* t) {
    spinlock_acquire(&thread_state_lock);
    SLIST_REMOVE(thread_list, t);
    spinlock_release(&thread_state_lock);
}

void scheduler_thread_block(struct thread* t) {
    spinlock_acquire(&thread_state_lock);
    t->state = THREAD_BLOCKED;
    spinlock_release(&thread_state_lock);

    scheduler_yield();
}

void scheduler_thread_sleep(struct thread* t, const struct timespec* tp) {
    timer_sleep_thread(t, tp);
    scheduler_thread_block(t);
}

void scheduler_thread_unblock(struct thread* t) {
    spinlock_acquire(&thread_state_lock);
    if (t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY;
    }
    spinlock_release(&thread_state_lock);
}

void scheduler_init(void) {
    isr_register_handler(SCHEDULER_IRQ_VECTOR, reschedule, NULL);
    klog("[scheduler] initialized scheduler\n");
}
