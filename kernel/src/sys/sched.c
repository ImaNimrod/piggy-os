#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/percpu.h>
#include <dev/lapic.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/log.h>

#define DEFAULT_TIMESLICE 20000

struct process* kernel_process;

static struct process* process_list;
static struct thread* thread_list;

static bool add_thread_to_list(struct thread** list, struct thread* thread) {
    struct thread* iter = *list;
    if (!iter) {
        *list = thread;
        return true;
    }

    while (iter->next) {
        iter = iter->next;
    }
    iter->next = thread;
    return true;
}

static bool remove_thread_from_list(struct thread** list, struct thread* thread) {
	struct thread* iter = *list;
	if (iter == thread) {
		*list = iter->next;
		return true;
	}

    struct thread* next = NULL;
	while (iter) {
		next = iter->next;
		if (next == thread) {
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
        iter = thread_list;
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

static void schedule(struct registers* r) {
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

    klog("running thread: %d\n", next->tid);

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
}

__attribute__((noreturn)) void sched_await(void) {
    lapic_timer_oneshot(SCHED_VECTOR, DEFAULT_TIMESLICE);
    sti();
    for (;;) {
        hlt();
    }
    __builtin_unreachable();
}

struct process* sched_get_kernel_process(void) {
    return kernel_process;
}

bool sched_enqueue_thread(struct thread* thread) {
    return add_thread_to_list(&thread_list, thread);
}

bool sched_dequeue_thread(struct thread* thread) {
    return remove_thread_from_list(&thread_list, thread);
}

void sched_init(void) {
    isr_install_handler(SCHED_VECTOR, false, schedule);
    kernel_process = process_create(vmm_get_kernel_pagemap());

    klog("[sched] intialized scheduler and created kernel process\n");
}
