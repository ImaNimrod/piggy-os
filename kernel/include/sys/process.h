#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H 1

#include <cpu/isr.h>
#include <stdbool.h>
#include <stdint.h>
#include <types.h>
#include <utils/spinlock.h>
#include <utils/vector.h>

enum thread_state {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
};

struct thread {
    tid_t tid;
    enum thread_state state;
    bool is_user;

    struct process* process;

    uintptr_t kernel_stack;

    struct registers registers;
    void* fpu_context;
    uint64_t fs_base;
    uint64_t gs_base;

    uint64_t sleep_until;

    spinlock_t run_lock;
    spinlock_t yield_lock;

    struct thread* next;
};

struct process {
    pid_t pid;

    struct pagemap* pagemap;

    tid_t next_tid;
    struct process* parent;
    vector_t* children;
    vector_t* threads;
};

extern struct process* kernel_process;

struct process* process_create(struct process* old_process, struct pagemap* pagemap);
struct thread* kthread_create(uintptr_t entry, void* arg);
void thread_destroy(struct thread* t);
void process_init(void);

#endif /* _KERNEL_SYS_PROCESS_H */
