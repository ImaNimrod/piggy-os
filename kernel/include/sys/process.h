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

#define PROCESS_BRK_BASE            0x60000000000
#define PROCESS_THREAD_STACK_TOP    0x70000000000

enum process_state {
    PROCESS_RUNNING,
    PROCESS_WAITING,
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
    uintptr_t code_base;
    uintptr_t brk;
    uintptr_t thread_stack_top;

    struct vfs_node* cwd;
    spinlock_t fd_lock;
    struct file_descriptor* file_descriptors[MAX_FDS];

    struct process* parent;
    vector_t* children;
    vector_t* threads;
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
bool process_create_init(void);
void process_destroy(struct process* p, int status);
void* process_sbrk(struct process* p, intptr_t size);

struct thread* thread_create(struct process* p, uintptr_t entry, void* arg, const char** argv, const char** envp, bool is_user);
struct thread* thread_fork(struct process* forked, struct thread* old_thread);
void thread_destroy(struct thread* t);

void process_init(void);

#endif /* _KERNEL_SYS_PROCESS_H */
