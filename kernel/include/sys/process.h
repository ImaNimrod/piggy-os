#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <cpu/isr.h>
#include <fs/file.h>
#include <mem/vmm.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/elf.h>
#include <sys/signal.h>
#include <types.h>
#include <utils/mutex.h>
#include <utils/spinlock.h>
#include <utils/vector.h>
#include <utils/wait_queue.h>

#define KERNEL_STACK_SIZE   0x8000
#define USER_STACK_SIZE     0x40000

#define INTERPRETER_LOAD_BASE 0x40000000

#define PROCESS_EXITCODE(ret, sig) (((sig) << 8) | (ret))

#define PROCESS_FD_COUNT    32
#define PROCESS_STACK_TOP   USER_END

#define THREAD_FLAG_USER                (1 << 0)
#define THREAD_FLAG_INTERRUPTABLE       (1 << 1)
#define THREAD_FLAG_RETURN_SIGNAL_MASK  (1 << 2)

typedef enum {
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_ZOMBIE,
} process_state_t;

typedef enum {
    THREAD_STATE_INIT = 0,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_WAITING,
} thread_state_t;

typedef enum {
    THREAD_WAKEUP_REASON_NORMAL,
    THREAD_WAKEUP_REASON_INTERRUPTED,
} thread_wakeup_reason_t;

struct cpu_local;
struct wait_queue;

struct thread {
    uintptr_t kernel_stack;
    uintptr_t kernel_stack_paddr;

    struct registers registers;
    void* fpu_context;
    uint64_t fs_base;
    uint64_t gs_base;

    struct cpu_local* cpu;
    struct process* process;

    tid_t tid;

    thread_state_t state;
    int flags;
    thread_wakeup_reason_t wakeup_reason;
    spinlock_t state_lock;

    sigset_t pending_signals;
    sigset_t signal_mask;
    sigset_t return_signal_mask;
    stack_t signal_stack;
    spinlock_t signal_lock;

    struct timespec time_used;

    struct registers* usercopy_registers;

    // for scheduler run queues
    struct thread* prev;
    struct thread* next;

    // for events, mutexes, semaphores
    struct thread* next_waiter;
};

struct vfs_node;

struct process {
    pid_t pid;
    process_state_t state;

    vector_t* threads;
    spinlock_t thread_list_lock;

    int exit_status;

    struct vfs_node* cwd;
    struct vfs_node* root;
    spinlock_t node_lock;

    struct file_descriptor fds[PROCESS_FD_COUNT];
    mutex_t fd_mutex;

    struct wait_queue child_wait;

    struct vmm_context* vmm_context;

    struct sigaction signal_actions[NSIG];
    spinlock_t signal_actions_lock;

    struct timespec time_used;

    struct process* parent;

    struct process* children;
    struct process* next;
};

extern struct process* kernel_process;
extern struct process* init_process;

struct process* process_create(struct process* parent);
void process_create_init(void);
void process_destroy(struct process* process);
void process_exit(struct process* process, int status);
struct process* process_find_by_pid(pid_t pid);
struct vfs_node* process_get_cwd(struct process* process);
struct vfs_node* process_get_root(struct process* process);
void process_set_cwd(struct process* process, struct vfs_node* new_cwd);
void process_set_root(struct process* process, struct vfs_node* new_root);

struct thread* thread_create_kernel(uintptr_t entry, void* arg);
struct thread* thread_create_user(struct process* process, uintptr_t entry, uintptr_t stack);
void thread_destroy(struct thread* thread);
struct thread* thread_fork(struct process* process, struct thread* old_thread, struct registers* context);

void process_init(void);

#endif /* _KERNEL_SYS_PROCESS_H */
