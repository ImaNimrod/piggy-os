#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <dev/hpet.h>
#include <dev/lapic.h>
#include <sys/sched.h>
#include <utils/log.h>
#include <utils/spinlock.h>

#define DEFAULT_TIMESLICE 20000

struct process* kernel_process;

static struct process* processes = NULL;
static struct thread* runnable_threads = NULL;
static struct thread* sleeping_threads = NULL;

static spinlock_t thread_lock = {0};

static bool add_thread_to_list(struct thread** list, struct thread* t) {
    struct thread* iter = *list;
    if (!iter) {
        *list = t;
        return true;
    }

    while (iter->next) {
        iter = iter->next;
    }
    iter->next = t;
    return true;
}

static bool remove_thread_from_list(struct thread** list, struct thread* t) {
	struct thread* iter = *list;
	if (iter == t) {
		*list = iter->next;
		return true;
	}

    struct thread* next = NULL;
	while (iter) {
		next = iter->next;
		if (next == t) {
			iter->next = next->next;
			next->next = NULL;
			return true;
		}

		iter = next;
	}
    return false;
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

static void wakeup_sleeping_threads(void) {
    struct thread* iter = sleeping_threads;
    while (iter) {
        if (iter->sleep_until <= hpet_count()) {
            iter->state = THREAD_READY_TO_RUN;
            iter->sleep_until = 0;

            remove_thread_from_list(&sleeping_threads, iter);
            add_thread_to_list(&runnable_threads, iter);
        }
        iter = iter->next;
    }
}

__attribute__((noreturn)) static void schedule(struct registers* r) {
    wakeup_sleeping_threads();

    struct thread* current = this_cpu()->current_thread;
    struct thread* next = get_next_thread(current);

    if (current) {
		current->ctx = *r;

        if (current->ctx.cs & 0x3) {
            this_cpu()->fpu_save(current->fpu_storage);
        }

        if (current->state == THREAD_NORMAL) {
            current->state = THREAD_READY_TO_RUN;
        }

        spinlock_release(&current->lock);
    }

    if (!next) {
        this_cpu()->current_thread = NULL;
        vmm_switch_pagemap(vmm_get_kernel_pagemap());
        lapic_eoi();
        sched_await();
    }

    this_cpu()->current_thread = next;
    next->state = THREAD_NORMAL;

    if (next->ctx.cs & 0x3) {
        swapgs();
        this_cpu()->fpu_restore(next->fpu_storage);
    }

    lapic_eoi();
    lapic_timer_oneshot(SCHED_VECTOR, next->timeslice);

    if (!current || current->process != next->process) {
        vmm_switch_pagemap(next->process->pagemap);
    }

    // klog("running thread #%d on cpu #%u\n", next->tid, this_cpu()->cpu_number);

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
		"iretq\n\t"
		:: "r" (&next->ctx)
	);
    __builtin_unreachable();
}

__attribute__((noreturn)) void sched_await(void) {
    lapic_timer_oneshot(SCHED_VECTOR, DEFAULT_TIMESLICE);
    sti();
    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

bool sched_enqueue_thread(struct thread* t) {
    return add_thread_to_list(&runnable_threads, t);
}

bool sched_dequeue_thread(struct thread* t) {
    return remove_thread_from_list(&runnable_threads, t);
}

void sched_thread_sleep(struct thread* t, uint64_t ns) {
    spinlock_acquire(&thread_lock);

    t->state = THREAD_SLEEPING;
    t->sleep_until = HPET_CALC_SLEEP_NS(ns);

    remove_thread_from_list(&runnable_threads, t);
    add_thread_to_list(&sleeping_threads, t);

    spinlock_release(&thread_lock);
    sched_resched_now();
}

void sched_init(void) {
    isr_install_handler(SCHED_VECTOR, false, schedule);
    kernel_process = process_create(vmm_get_kernel_pagemap());

    klog("[sched] intialized scheduler and created kernel process\n");
}
