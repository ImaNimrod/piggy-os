#include <cpu/smp.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define KERNEL_STACK_SIZE   0x4000
#define USER_STACK_SIZE     0x20000
#define VMM_MAP_STACK_TOP   0x700000000

struct process* kernel_process = NULL;
static struct process* init_process = NULL;

static struct slab_cache* process_cache = NULL;
static struct slab_cache* thread_cache = NULL;
static pid_t next_pid = 0;

static const uint16_t default_fcw = 0x33f;
static const uint32_t default_mxcsr = 0x1f80;

struct process* process_create(struct process* parent, struct pagemap* pagemap) {
    struct process* new_process = slab_cache_alloc(process_cache);
    if (unlikely(new_process == NULL)) {
        return NULL;
    }

    new_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(new_process->threads == NULL)) {
        goto error;
    }

    if (parent != NULL) {
        new_process->parent = parent;
        SLIST_PUSH_FRONT(parent->children, new_process);

        kpanic(NULL, false, "not supported");
    } else {
        new_process->pagemap = pagemap;
    }

    new_process->pid = __atomic_load_n(&next_pid, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    goto end;

error:
    if (new_process->threads != NULL) {
        vector_destroy(new_process->threads);
    }

    slab_cache_free(process_cache, new_process);
    new_process = NULL;
end:
    return new_process;
}

void process_create_init(void) {
    struct pagemap* init_pagemap = pagemap_create();
    if (unlikely(init_pagemap == NULL)) {
        kpanic(NULL, false, "failed to create pagemap for init process");
    }

    init_process = process_create(NULL, init_pagemap);
    if (unlikely(init_process == NULL)) {
        kpanic(NULL, false, "failed to create init process");
    }

    uintptr_t entry;

    if (!elf_load(init_pagemap, &entry)) {
        kpanic(NULL, false, "failed to load ELF for init process");
    }

    struct thread* init_thread = thread_create_user(init_process, entry);
    if (unlikely(init_thread == NULL)) {
        kpanic(NULL, false, "failed to create thread for init process");
    }
}

void process_destroy(struct process* process) {
    if (unlikely(process->pid == 1)) {
        kpanic(NULL, false, "attempted to destroy init process");
    }

    if (likely(process->parent != NULL)) {
        SLIST_REMOVE(process->parent->children, process);
    }

    /* reparent dying process' children to init */
    struct process* child = process->children;
    while (child != NULL) {
        struct process* next = child->next;

        child->parent = init_process;
        child->next = init_process->children;
        init_process->children = child;

        child = next;
    }

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        struct thread* thread = *vector_get(process->threads, i);
        scheduler_thread_dequeue(thread);
        thread_destroy(thread);
    }
    vector_destroy(process->threads);

    pagemap_destroy(process->pagemap);

    slab_cache_free(process_cache, process);
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    thread->state = THREAD_READY;
    thread->is_user = false;
    thread->process = kernel_process;

    thread->kernel_stack = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB) + HIGH_VMA + KERNEL_STACK_SIZE;

    thread->registers.rdi = (uint64_t) arg;
    thread->registers.rip = entry;
    thread->registers.cs = 0x08;
    thread->registers.rflags = 0x202;
    thread->registers.ss = 0x10;
    thread->registers.rsp = thread->kernel_stack;

    thread->tid = vector_size(kernel_process->threads);
    vector_push(kernel_process->threads, &thread);

    scheduler_thread_enqueue(thread);

    return thread;
}

struct thread* thread_create_user(struct process* process, uintptr_t entry) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    thread->state = THREAD_READY;
    thread->is_user = true;
    thread->process = process;

    thread->kernel_stack = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB) + HIGH_VMA + KERNEL_STACK_SIZE;

    if (unlikely(!vmm_map(process->pagemap, VMM_MAP_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE, VMM_FLAG_PROT_READ | VMM_FLAG_PROT_WRITE))) {
        pmm_free(thread->kernel_stack - HIGH_VMA, KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
        slab_cache_free(thread_cache, thread);
        return NULL;
    }

    thread->registers.rip = entry;
    thread->registers.cs = 0x23;
    thread->registers.rflags = 0x202;
    thread->registers.ss = 0x1b;
    thread->registers.rsp = VMM_MAP_STACK_TOP;

    thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    this_cpu()->fpu_restore(thread->fpu_context);
    asm volatile("fldcw %0" :: "m"(default_fcw) : "memory");
    asm volatile("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");
    this_cpu()->fpu_save(thread->fpu_context);

    thread->tid = vector_size(process->threads);
    vector_push(process->threads, &thread);

    scheduler_thread_enqueue(thread);

    return thread;
}

void thread_destroy(struct thread* thread) {
    scheduler_thread_dequeue(thread);

    vector_remove_by_value(thread->process->threads, thread);
    if (vector_size(thread->process->threads) < 1) {
        process_destroy(thread->process);
    }

    pmm_free(thread->kernel_stack - HIGH_VMA, KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    slab_cache_free(thread_cache, thread);
}

void process_init(void) {
    process_cache = slab_cache_create("struct process cache", sizeof(struct process));
    if (unlikely(process_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for process structs");
    }

    thread_cache = slab_cache_create("struct thread cache", sizeof(struct thread));
    if (unlikely(process_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for thread structs");
    }

    kernel_process = process_create(NULL, kernel_pagemap);
    if (unlikely(kernel_process == NULL)) {
        kpanic(NULL, false, "failed to create kernel process");
    }

    klog("[process] initialized kernel process\n");
}
