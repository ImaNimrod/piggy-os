#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <cpu/isr.h>
#include <fs/fd.h>
#include <fs/vfs.h>
#include <mem/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <utils/string.h>
#include <utils/vector.h>

enum process_state {
    PROCESS_RUNNING,
    PROCESS_ZOMBIE,
};

enum thread_state {
    THREAD_NORMAL = 0,
    THREAD_READY_TO_RUN,
};

struct thread;

struct process {
    pid_t pid;
    enum process_state state;
    char name[256];
    uint8_t exit_code;

    struct pagemap* pagemap;
    uintptr_t thread_stack_top;

    struct vfs_node* cwd;
    spinlock_t fd_lock;
    struct file_descriptor* file_descriptors[MAX_FDS];

    struct process* parent;
    vector_t* children;
    vector_t* threads;

    struct process* next;
};

struct thread {
    tid_t tid;
    enum thread_state state;
    struct process* process;
    spinlock_t lock;
    uint64_t timeslice;

    uintptr_t kernel_stack;
    uintptr_t page_fault_stack;
    uintptr_t stack;
    struct registers ctx;
    void* fpu_storage;
    uint64_t fs_base;
    uint64_t gs_base;

    struct thread* next;
};

struct process* process_create(struct process* old, struct pagemap* pagemap);
void process_destroy(struct process* p, int status);

struct thread* thread_create(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp, bool is_user);
struct thread* thread_fork(struct process* forked, struct thread* old_thread);
void thread_destroy(struct thread* t);

#endif /* _KERNEL_SYS_PROCESS_H */
