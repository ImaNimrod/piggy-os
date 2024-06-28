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

static struct thread* runnable_threads = NULL;

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

__attribute__((noreturn)) static void schedule(struct registers* r) {
    lapic_timer_stop();

    struct thread* current = this_cpu()->running_thread;
    struct thread* next = get_next_thread(current);

    if (current) {
		current->ctx = *r;

        if (current->ctx.cs & 3) {
            this_cpu()->fpu_save(current->fpu_storage);
        }

        current->fs_base = rdmsr(MSR_FS_BASE);
        current->gs_base = rdmsr(MSR_KERNEL_GS);

        if (current->state == THREAD_NORMAL) {
            current->state = THREAD_READY_TO_RUN;
        }

        spinlock_release(&current->lock);
    }

    if (!next) {
        this_cpu()->running_thread = NULL;
        vmm_switch_pagemap(&kernel_pagemap);
        lapic_eoi();
        sched_await();
    }

    this_cpu()->running_thread = next;
    this_cpu()->tss.rsp0 = next->kernel_stack;
    this_cpu()->tss.ist2 = next->page_fault_stack;
    this_cpu()->user_stack = next->stack;
    this_cpu()->kernel_stack = next->kernel_stack;
    next->state = THREAD_NORMAL;

    lapic_eoi();
    lapic_timer_oneshot(SCHED_VECTOR, next->timeslice);

    wrmsr(MSR_FS_BASE, next->fs_base);
    if (next->ctx.cs & 3) {
        this_cpu()->fpu_restore(next->fpu_storage);
        wrmsr(MSR_KERNEL_GS, next->gs_base);
    }

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
        "swapgs\n\t"
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

bool sched_thread_enqueue(struct thread* t) {
    spinlock_acquire(&thread_lock);
    bool ret = add_thread_to_list(&runnable_threads, t);
    spinlock_release(&thread_lock);
    return ret;
}

bool sched_thread_dequeue(struct thread* t) {
    spinlock_acquire(&thread_lock);
    bool ret = remove_thread_from_list(&runnable_threads, t);
    spinlock_release(&thread_lock);
    return ret;
}

void sched_thread_destroy(struct thread* t) {
    vector_remove_by_value(t->process->threads, t);
    sched_thread_dequeue(t);
    thread_destroy(t);
}

__attribute__((noreturn)) void sched_yield(void) {
    cli();

    lapic_timer_stop();
    lapic_send_ipi(this_cpu()->lapic_id, SCHED_VECTOR);

    sti();
    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

void sched_init(void) {
    isr_install_handler(SCHED_VECTOR, false, schedule);
    kernel_process = process_create("kernel_process", &kernel_pagemap);

    klog("[sched] intialized scheduler and created kernel process\n");
}
