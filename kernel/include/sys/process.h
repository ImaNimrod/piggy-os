#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <cpu/isr.h>
#include <fs/file.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/elf.h>
#include <types.h>
#include <utils/spinlock.h>
#include <utils/vector.h>

#define PROCESS_FD_COUNT    32
#define PROCESS_BRK_BASE    0x600000000
#define PROCESS_STACK_TOP   0x700000000

typedef enum {
    PROCESS_RUNNING,
    PROCESS_ZOMBIE,
} process_state_t;

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
} thread_state_t;

struct thread {
    uintptr_t kernel_stack;

    uintptr_t kernel_stack_paddr;
    uintptr_t user_stack_paddr;

    struct registers registers;
    void* fpu_context;
    uint64_t fs_base;
    uint64_t gs_base;

    tid_t tid;
    thread_state_t state;
    bool is_user;
    struct timespec time_used;

    struct process* process;

    struct registers* usercopy_registers;

    spinlock_t run_lock;
    spinlock_t yield_lock;

    struct thread* next;
    struct thread* next_waiter; // for semaphores
};

struct vfs_node;

struct process {
    pid_t pid;
    process_state_t state;
    int exit_status;
    struct vfs_node* cwd;
    struct timespec time_used;

    struct file_descriptor fds[PROCESS_FD_COUNT];
    spinlock_t fd_lock;

    struct pagemap* pagemap;
    uintptr_t thread_stack_top;

    uintptr_t brk;
    uintptr_t brk_next_unallocated_page_begin;

    struct process* parent;

    struct process* children;
    struct process* next;

    tid_t next_tid;
    vector_t* threads;

    spinlock_t lock;
};

extern struct process* kernel_process;

struct process* process_create(struct process* old_process, struct pagemap* pagemap);
void process_create_init(void);
void process_destroy(struct process* process);
void process_exit(struct process* process, int status);
void* process_sbrk(struct process* process, intptr_t size);

struct thread* thread_create_kernel(uintptr_t entry, void* arg);
struct thread* thread_create_user(struct process* process, uintptr_t entry, char** argv, char** envp, struct auxvals* auxvals);
void thread_destroy(struct thread* thread);
struct thread* thread_fork(struct process* process, struct thread* old_thread);

void process_init(void);

#endif /* _KERNEL_SYS_PROCESS_H */
