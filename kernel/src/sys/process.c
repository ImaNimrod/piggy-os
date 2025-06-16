#include <cpu/smp.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>

#define STACK_SIZE 0x2000

struct process* kernel_process = NULL;

static struct slab_cache* process_cache = NULL;
static struct slab_cache* thread_cache = NULL;
static pid_t next_pid;

struct process* process_create(struct process* old_process, struct pagemap* pagemap) {
    struct process* new_process = slab_cache_alloc(process_cache);
    if (unlikely(new_process == NULL)) {
        return NULL;
    }

    new_process->children = vector_create(sizeof(struct process*));
    if (unlikely(new_process->children == NULL)) {
        goto error;
    }

    new_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(new_process->threads == NULL)) {
        goto error;
    }

    if (old_process != NULL) {
        kpanic(NULL, false, "not supported");
    } else {
        new_process->pagemap = pagemap;
    }

    new_process->pid = next_pid;
    __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    goto end;

error:
    if (new_process->children != NULL) {
        vector_destroy(new_process->children);
    }
    if (new_process->threads != NULL) {
        vector_destroy(new_process->threads);
    }

    slab_cache_free(process_cache, new_process);
    new_process = NULL;
end:
    return new_process;
}

struct thread* kthread_create(uintptr_t entry, void* arg) {
    struct thread* new_thread = slab_cache_alloc(thread_cache);
    if (unlikely(new_thread == NULL)) {
        return NULL;
    }

    new_thread->state = THREAD_READY;
    new_thread->is_user = false;
    new_thread->process = kernel_process;

    new_thread->run_lock = (spinlock_t) {0};
    new_thread->yield_lock = (spinlock_t) {0};
    new_thread->next = NULL;

    new_thread->kernel_stack = pmm_alloc(STACK_SIZE / PAGE_SIZE) + HIGH_VMA;

    new_thread->registers.rdi = (uint64_t) arg;
    new_thread->registers.rip = entry;
    new_thread->registers.cs = 0x08;
    new_thread->registers.rflags = 0x202;
    new_thread->registers.ss = 0x10;
    new_thread->registers.rsp = new_thread->kernel_stack;

    new_thread->tid = vector_size(kernel_process->threads);
    vector_push(kernel_process->threads, &new_thread);

    return new_thread;
}

void thread_destroy(struct thread* t) {
    vector_remove_by_value(t->process->threads, t);
    pmm_free(t->kernel_stack - HIGH_VMA, STACK_SIZE / PAGE_SIZE);
    slab_cache_free(thread_cache, t);
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
