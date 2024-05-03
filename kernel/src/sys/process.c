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

struct process* process_create(const char* name, struct pagemap* pagemap) {
    struct process* p = kcalloc(1, sizeof(struct process));

    p->pid = next_pid++;
    memcpy(p->name, name, sizeof(p->name) - 1);
    p->pagemap = pagemap;
    p->threads = (typeof(p->threads)) {0};

    return p;
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* t = kmalloc(sizeof(struct thread));

    t->tid = next_tid++;
    t->state = THREAD_READY_TO_RUN;
    t->process = kernel_process;
    t->sleep_until = 0;
    t->timeslice = 5000;
    t->lock = (spinlock_t) {0};

    t->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
	t->kernel_stack += STACK_SIZE;

    t->page_fault_stack = t->kernel_stack;
    t->stack = t->kernel_stack;

    t->ctx.cs = 0x08;
    t->ctx.ss = 0x10;
    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;
    t->ctx.rdi = (uint64_t) arg;
    t->ctx.rsp = t->kernel_stack;

    VECTOR_PUSH_BACK(&kernel_process->threads, t);

    return t;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    
    if (t->ctx.cs & 0x3) {
		pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free((uintptr_t) t->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }

    kfree(t);
}
