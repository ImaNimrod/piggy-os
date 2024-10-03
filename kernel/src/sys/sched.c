#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <sys/sched.h>
#include <utils/log.h>
#include <utils/spinlock.h>

struct process* kernel_process;

static struct thread* runnable_threads = NULL;
static struct thread* blocking_threads = NULL;
static struct thread* zombie_threads = NULL;
static spinlock_t thread_list_lock = {0};
static spinlock_t thread_management_lock = {0};

static void add_thread_to_list(struct thread** list, struct thread* t) {
    spinlock_acquire(&thread_list_lock);

    struct thread* iter = *list;
    if (!iter) {
        *list = t;
        spinlock_release(&thread_list_lock);
        return;
    }

    while (iter->next) {
        iter = iter->next;
    }
    iter->next = t;
    spinlock_release(&thread_list_lock);
}

static void remove_thread_from_list(struct thread** list, struct thread* t) {
    spinlock_acquire(&thread_list_lock);

    struct thread* iter = *list;
    if (iter == t) {
        *list = iter->next;
        iter->next = NULL;
        spinlock_release(&thread_list_lock);
        return;
    }

    struct thread* next = NULL;
    while (iter) {
        next = iter->next;
        if (next == t) {
            iter->next = next->next;
            next->next = NULL;
            spinlock_release(&thread_list_lock);
            return;
        }

        iter = next;
    }

    spinlock_release(&thread_list_lock);
}

static struct thread* get_next_thread(struct thread* current) {
    struct thread* iter = NULL;
    if (current) {
        iter = current->next;
    } else {
        iter = runnable_threads;
    }

    while (iter) {
        if (iter->state != THREAD_READY_TO_RUN) {
            iter = iter->next;
            continue;
        }

        if (spinlock_test_and_acquire(&iter->lock)) {
            return iter;
        }
        iter = iter->next;
    }

    return NULL;
}

__attribute__((noreturn)) static void schedule(struct registers* r, void* ctx) {
    (void) ctx;

    lapic_timer_stop();

    if (spinlock_test_and_acquire(&thread_management_lock)) {
        struct thread* iter = blocking_threads;
        while (iter != NULL) {
            if (iter->state == THREAD_SLEEPING && iter->sleep_until < hpet_count()) {
                iter->state = THREAD_READY_TO_RUN;
                iter->sleep_until = 0;

                remove_thread_from_list(&blocking_threads, iter);
                add_thread_to_list(&runnable_threads, iter);
            }
            iter = iter->next;
        }

        iter = zombie_threads;
        while (iter != NULL) {
            thread_destroy(iter);

            struct process* p = iter->process;
            if (p != kernel_process && p->state == PROCESS_ZOMBIE && p->threads->size == 0) {
                klog("[sched] destroying process (pid: %d)\n", p->pid);
                process_destroy(p);
            }

            remove_thread_from_list(&zombie_threads, iter);
            iter = iter->next;
        }

        spinlock_release(&thread_management_lock);
    }

    struct thread* current = this_cpu()->running_thread;
    struct thread* next = get_next_thread(current);

    if (current != NULL) {
        current->ctx = *r;

        if (current->is_user) {
            current->user_stack = this_cpu()->user_stack;
            this_cpu()->fpu_save(current->fpu_storage);
            current->gs_base = rdmsr(IA32_KERNEL_GS_BASE_MSR);
        }

        current->fs_base = rdmsr(IA32_FS_BASE_MSR);

        if (current->state == THREAD_RUNNING) {
            current->state = THREAD_READY_TO_RUN;
        }

        spinlock_release(&current->lock);
    }

    if (next == NULL) {
        this_cpu()->running_thread = NULL;
        vmm_switch_pagemap(kernel_pagemap);
        lapic_eoi();
        sched_await();
    }

    this_cpu()->running_thread = next;
    next->state = THREAD_RUNNING;

    this_cpu()->tss.rsp0 = next->kernel_stack;
    this_cpu()->tss.ist2 = next->page_fault_stack;
    this_cpu()->kernel_stack = next->kernel_stack;

    lapic_eoi();
    lapic_timer_oneshot(SCHED_VECTOR, next->timeslice);

    if (next->is_user) {
        this_cpu()->user_stack = next->user_stack;
        this_cpu()->fpu_restore(next->fpu_storage);
        wrmsr(IA32_KERNEL_GS_BASE_MSR, next->gs_base);
    }

    wrmsr(IA32_FS_BASE_MSR, next->fs_base);

    if (!current || current->process != next->process) {
        vmm_switch_pagemap(next->process->pagemap);
    }

    __asm__ volatile(
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
            "cmpq $0x23, 8(%%rsp)\n\t"
            "jne 1f\n\t"
            "swapgs\n\t"
            "1:\n\t"
            "iretq\n\t"
            :: "r" (&next->ctx)
            );
    __builtin_unreachable();
}

__attribute__((noreturn)) void sched_await(void) {
    lapic_timer_oneshot(SCHED_VECTOR, 5000);
    sti();
    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

void sched_thread_enqueue(struct thread* t) {
    add_thread_to_list(&runnable_threads, t);
}

void sched_thread_dequeue(struct thread* t) {
    remove_thread_from_list(&runnable_threads, t);
    t->state = THREAD_ZOMBIE;
    add_thread_to_list(&zombie_threads, t);
}

void sched_thread_sleep(struct thread* t, uint64_t ns) {
    remove_thread_from_list(&runnable_threads, t);

    t->state = THREAD_SLEEPING;
    t->sleep_until = HPET_CALC_SLEEP_NS(ns);
    add_thread_to_list(&blocking_threads, t);

    sched_yield();
}

__attribute__((section(".unmap_after_init")))
void sched_init(void) {
    isr_install_handler(SCHED_VECTOR, schedule, NULL);
    kernel_process = process_create(NULL, kernel_pagemap);

    klog("[sched] intialized scheduler and created kernel process\n");
}
