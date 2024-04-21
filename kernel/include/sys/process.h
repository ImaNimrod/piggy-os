#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <cpu/isr.h>
#include <mem/vmm.h>
#include <stdint.h>
#include <types.h>
#include <utils/string.h>
#include <utils/vector.h>

enum thread_state {
    THREAD_NORMAL = 0,
    THREAD_READY_TO_RUN,
    THREAD_SLEEPING,
};

struct thread;

struct process {
    pid_t pid;
    char name[256];
    struct pagemap* pagemap;
    VECTOR_TYPE(struct thread*) threads;
    struct process* next;
};

struct thread {
    struct thread* self;
    uint64_t errno;

    tid_t tid;
    enum thread_state state;
    struct process* process;
    uint64_t sleep_until;
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

struct process* process_create(struct pagemap* pagemap);
struct thread* thread_create_kernel(uintptr_t entry, void* arg);

#endif /* _KERNEL_SYS_PROCESS_H */
