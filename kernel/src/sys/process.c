#include <cpu/percpu.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/math.h>

#define STACK_SIZE 0x40000

static pid_t next_pid = 0;
static tid_t next_tid = 0;

struct process* process_create(struct pagemap* pagemap) {
    struct process* new_process = kmalloc(sizeof(struct process));

    new_process->pid = next_pid++;
    new_process->pagemap = pagemap;

    new_process->threads = (typeof(new_process->threads)) {0};

    return new_process;
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* thread = kmalloc(sizeof(struct thread));

    thread->self = thread;
    thread->tid = next_tid++;
    thread->state = THREAD_READY_TO_RUN;
    thread->process = kernel_process;
    thread->sleep_until = 0;
    thread->timeslice = 5000;
    thread->lock = (spinlock_t) {0};

    thread->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
	thread->kernel_stack += STACK_SIZE;

    thread->page_fault_stack = thread->kernel_stack;
    thread->stack = thread->kernel_stack;

    thread->ctx.cs = 0x08;
    thread->ctx.ss = 0x10;
    thread->ctx.rflags = 0x202;
    thread->ctx.rip = entry;
    thread->ctx.rdi = (uint64_t) arg;
    thread->ctx.rsp = thread->kernel_stack;

    VECTOR_PUSH_BACK(&kernel_process->threads, thread);

    return thread;
}

void thread_destroy(struct thread* thread) {
    pmm_free(thread->kernel_stack - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    
    if (thread->ctx.cs & 0x3) {
        pmm_free((uintptr_t) thread->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }
}
