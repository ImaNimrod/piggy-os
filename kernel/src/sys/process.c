#include <cpu/percpu.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/math.h>

#define STACK_SIZE 0x40000

static pid_t next_pid = 0;
static tid_t next_tid = 0;

struct process* process_create(const char* name, struct pagemap* pagemap) {
    struct process* p = kmalloc(sizeof(struct process));

    p->pid = next_pid++;
    memcpy(p->name, name, sizeof(p->name) - 1);

    p->pagemap = pagemap;
    p->thread_stack_top = 0x70000000000;

    p->children = vector_new(sizeof(struct process*));
    p->threads = vector_new(sizeof(struct thread*));

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

    t->fs_base = rdmsr(MSR_FS_BASE);
    t->gs_base = rdmsr(MSR_KERNEL_GS);

    vector_push_back(kernel_process->threads, t);

    return t;
}

struct thread* thread_create_user(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp) {
    (void) argv;
    (void) envp;

    struct thread* t = kmalloc(sizeof(struct thread));

    t->tid = next_tid++;
    t->state = THREAD_READY_TO_RUN;
    t->process = p;
    t->sleep_until = 0;
    t->timeslice = 10000;
    t->lock = (spinlock_t) {0};

    t->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
	t->kernel_stack += STACK_SIZE;

    t->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE) + HIGH_VMA;
	t->page_fault_stack += STACK_SIZE;

    size_t stack_pages = DIV_CEIL(STACK_SIZE, PAGE_SIZE);
    uintptr_t stack_paddr = pmm_alloc(stack_pages);

    for (size_t i = 0; i < stack_pages; i++) {
        p->pagemap->map_page(p->pagemap,
                p->thread_stack_top - STACK_SIZE + (i * PAGE_SIZE),
                stack_paddr - STACK_SIZE + (i * PAGE_SIZE),
                PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
    }

    t->ctx.rsp = p->thread_stack_top;
    p->thread_stack_top -= STACK_SIZE - PAGE_SIZE;
    t->stack = t->ctx.rsp;

    t->ctx.cs = 0x23;
    t->ctx.ss = 0x1b;
    t->ctx.rflags = 0x002;
    t->ctx.rip = entry;
    t->ctx.rdi = (uint64_t) arg;

    t->fs_base = 0;
    t->gs_base = 0;

    vector_push_back(p->threads, t);

    return t;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    
    if (t->ctx.cs & 0x3) {
		pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    }

    kfree(t);
}
