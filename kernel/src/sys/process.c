#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/process.h>
#include <sys/sched.h>
#include <utils/math.h>
#include <utils/string.h>

#define STACK_SIZE 0x40000

static pid_t next_pid = 0;

struct process* process_create(const char* name, struct pagemap* pagemap) {
    struct process* p = kmalloc(sizeof(struct process));

    p->pid = next_pid;
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    memcpy(p->name, name, sizeof(p->name) - 1);

    p->pagemap = pagemap;
    p->thread_stack_top = 0x70000000000;

    p->cwd = vfs_get_root();

    p->children = vector_create(sizeof(struct process*));
    p->threads = vector_create(sizeof(struct thread*));

    return p;
}

void process_destroy(struct process* p) {
    // TODO: properly destroy vmm pagemaps 
    vector_destroy(p->children);
    vector_destroy(p->threads);
    kfree(p);
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* t = kmalloc(sizeof(struct thread));

    t->state = THREAD_READY_TO_RUN;
    t->process = kernel_process;
    t->sleep_until = 0;
    t->timeslice = 5000;
    t->lock = (spinlock_t) {0};

    uintptr_t stack_paddr = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (!stack_paddr) {
        kfree(t);
        return NULL;

    }

    uintptr_t stack_top = stack_paddr + STACK_SIZE + HIGH_VMA;

    t->kernel_stack = stack_top;
    t->page_fault_stack = stack_top;
    t->stack = stack_top;

    t->ctx.cs = 0x08;
    t->ctx.ss = 0x10;
    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;
    t->ctx.rdi = (uint64_t) arg;
    t->ctx.rsp = stack_top;

    t->fs_base = rdmsr(MSR_FS_BASE);
    t->gs_base = rdmsr(MSR_KERNEL_GS);

    t->tid = kernel_process->threads->size;
    vector_push_back(kernel_process->threads, t);

    return t;
}

struct thread* thread_create_user(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp) {
    (void) argv;
    (void) envp;

    struct thread* t = kmalloc(sizeof(struct thread));

    t->state = THREAD_READY_TO_RUN;
    t->process = p;
    t->sleep_until = 0;
    t->timeslice = 10000;
    t->lock = (spinlock_t) {0};

    uintptr_t stack_paddr = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    if (!stack_paddr) {
        kfree(t);
        return NULL;
    }

    for (size_t i = 0; i < ALIGN_UP(STACK_SIZE, PAGE_SIZE) / PAGE_SIZE + 1; i++) {
        bool ret = p->pagemap->map_page(p->pagemap,
                (p->thread_stack_top - STACK_SIZE) + (i * PAGE_SIZE),
                stack_paddr + (i * PAGE_SIZE),
                PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);

        if (!ret) {
            pmm_free(stack_paddr, STACK_SIZE / PAGE_SIZE);
            kfree(t);
            return NULL;
        }
    }

    t->ctx.rsp = t->stack = p->thread_stack_top;
    p->thread_stack_top -= STACK_SIZE;

    t->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    t->kernel_stack += STACK_SIZE + HIGH_VMA;

    t->page_fault_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    t->page_fault_stack += STACK_SIZE + HIGH_VMA;

    t->ctx.cs = 0x23;
    t->ctx.ss = 0x1b;
    t->ctx.rflags = 0x202;
    t->ctx.rip = entry;
    t->ctx.rdi = (uint64_t) arg;

    t->fpu_storage = (void*) (pmm_allocz(DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE)) + HIGH_VMA);

    this_cpu()->fpu_restore(t->fpu_storage);
    uint16_t default_fcw = 0x33f;
    __asm__ volatile ("fldcw %0" :: "m" (default_fcw) : "memory");
    uint32_t default_mxcsr = 0x1f80;
    __asm__ volatile ("ldmxcsr %0" :: "m" (default_mxcsr) : "memory");
    this_cpu()->fpu_save(t->fpu_storage);

    t->fs_base = 0;
    t->gs_base = 0;

    if (p->threads->size == 0 && argv != NULL && envp != NULL) {
        uintptr_t* stack = (uintptr_t*) (stack_paddr + STACK_SIZE + HIGH_VMA);
        void* stack_top = stack;

        int envp_len;
        for (envp_len = 0; envp[envp_len] != NULL; envp_len++) {
            size_t length = strlen(envp[envp_len]);
            stack = (uintptr_t*) ((uintptr_t) stack - length - 1);
            memcpy(stack, envp[envp_len], length);
        }

        int argv_len;
        for (argv_len = 0; argv[argv_len] != NULL; argv_len++) {
            size_t length = strlen(argv[argv_len]);
            stack = (uintptr_t*) ((uintptr_t) stack - length - 1);
            memcpy(stack, argv[argv_len], length);
        }

        stack = (uintptr_t*) ALIGN_DOWN((uintptr_t) stack, 16);
        if ((argv_len + envp_len + 1) & 1) {
            stack--;
        }

        uintptr_t old_rsp = t->ctx.rsp;

        *(--stack) = 0;
        stack -= envp_len;
        for (int i = 0; i < envp_len; i++) {
            old_rsp -= strlen(envp[i]) + 1;
            stack[i] = old_rsp;
        }

        *(--stack) = 0;
        stack -= argv_len;
        for (int i = 0; i < argv_len; i++) {
            old_rsp -= strlen(argv[i]) + 1;
            stack[i] = old_rsp;
        }

        *(--stack) = argv_len;

        t->ctx.rsp -= (uintptr_t) stack_top - (uintptr_t) stack;
    }

    t->tid = p->threads->size;
    vector_push_back(p->threads, t);

    return t;
}

void thread_destroy(struct thread* t) {
    pmm_free(t->kernel_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);

    if (t->ctx.cs & 0x3) {
        pmm_free(t->page_fault_stack - STACK_SIZE - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
        pmm_free((uintptr_t) t->fpu_storage - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_storage_size, PAGE_SIZE));
    }

    kfree(t);
}
