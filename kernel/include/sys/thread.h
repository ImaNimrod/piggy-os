#ifndef _KERNEL_SYS_THREAD_H
#define _KERNEL_SYS_THREAD_H

#include <cpu/isr.h>
#include <stdint.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <types.h>
#include <utils/spinlock.h>

enum thread_state {
    THREAD_NORMAL = 0,
    THREAD_READY_TO_RUN,
};

struct thread {
    struct thread* self;
    uint64_t errno;

    tid_t tid;
    enum thread_state state;
    struct process* process;
    uint64_t timeslice;
    spinlock_t lock;

    struct registers ctx;
    void* fpu_storage;
    uintptr_t kernel_stack;
    uintptr_t page_fault_stack;
    uintptr_t stack;
    uint64_t fs_base;
	uint64_t gs_base;

    struct thread* next;
};

struct thread* thread_create_kernel(uintptr_t entry, void* arg);

#endif /* _KERNEL_SYS_THREAD_H */
