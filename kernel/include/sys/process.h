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
    uint8_t exit_code;

    struct pagemap* pagemap;
    uintptr_t thread_stack_top;

    struct process* parent;
    vector_t* children;
    vector_t* threads;

    struct process* next;
};

struct thread {
    uint64_t errno;

    tid_t tid;
    enum thread_state state;
    struct process* process;
    uint64_t sleep_until;
    uint64_t timeslice;
    spinlock_t lock;

    struct registers ctx;
    uintptr_t kernel_stack;
    uintptr_t page_fault_stack;
    uintptr_t stack;
    uint64_t fs_base;
    uint64_t gs_base;

    struct thread* next;
};

struct process* process_create(const char* name, struct pagemap* pagemap);
struct thread* thread_create_kernel(uintptr_t entry, void* arg);
struct thread* thread_create_user(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp);
void thread_destroy(struct thread* t);

#endif /* _KERNEL_SYS_PROCESS_H */