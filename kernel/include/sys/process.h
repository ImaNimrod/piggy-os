#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <cpu/isr.h>
#include <fs/file.h>
#include <mem/vmm.h>
#include <stdint.h>
#include <sys/elf.h>
#include <sys/signal.h>
#include <stdatomic.h>
#include <types.h>
#include <utils/hashmap.h>
#include <utils/mutex.h>
#include <utils/spinlock.h>
#include <utils/vector.h>
#include <utils/wait_queue.h>

#define KERNEL_STACK_SIZE   0x4000
#define USER_STACK_SIZE     0x20000

#define INTERPRETER_LOAD_BASE 0x40000000

#define PROCESS_EXITCODE(ret, sig) (((sig) << 8) | (ret))

#define PROCESS_NAME_MAX    64
#define PROCESS_FD_COUNT    32
#define PROCESS_STACK_TOP   USER_END

#define THREAD_FLAG_USER                (1 << 0)
#define THREAD_FLAG_INTERRUPTABLE       (1 << 1)
#define THREAD_FLAG_RETURN_SIGNAL_MASK  (1 << 2)
#define THREAD_FLAG_SHOULD_EXIT         (1 << 3)

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

struct cpu_local;

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
    int wakeup_reason;
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

    // for wait_queues
    struct wait_node wait_node;
};

struct vfs_node;

struct process_group {
    pid_t pgid;

    struct process* head;
    struct process* tail;
    mutex_t mutex;

    atomic_size_t refcount;
};

struct process {
    char name[PROCESS_NAME_MAX];
    pid_t pid;
    process_state_t state;

    char** cmdline;

    int exit_status;
    spinlock_t exiting;

    vector_t* threads;
    spinlock_t thread_list_lock;

    struct vfs_node* cwd;
    struct vfs_node* root;
    spinlock_t node_lock;

    struct file_descriptor fds[PROCESS_FD_COUNT];
    mutex_t fd_mutex;

    struct vmm_context* vmm_context;

    struct sigaction signal_actions[NSIG - 1];
    spinlock_t signal_actions_lock;

    struct timespec time_used;

    struct process* parent;

    struct process* children;
    struct process* sibling_next;
    spinlock_t child_list_lock;

    struct wait_queue child_wq;

    struct process_group* group;
    struct process* group_prev;
    struct process* group_next;
    mutex_t group_mutex;

    // for global process list
    struct process* prev;
    struct process* next;
};

extern struct process* kernel_process;
extern struct process* init_process;

struct process* process_create(struct process* parent);
void process_create_init(void);
void process_destroy(struct process* process);
[[noreturn]] void process_exit(int status);
struct process* process_find(pid_t pid);
struct vfs_node* process_get_cwd(struct process* process);
struct vfs_node* process_get_root(struct process* process);
pid_t process_prev_pid(pid_t pid);
pid_t process_next_pid(pid_t pid);
void process_set_cwd(struct process* process, struct vfs_node* new_cwd);
void process_set_root(struct process* process, struct vfs_node* new_root);
void process_stop_all_threads(void);
void process_zombify(void);

struct process_group* process_group_create(pid_t pgid);
struct process_group* process_group_find(pid_t pgid);
void process_group_move(struct process_group* new_group, struct process* process);
void process_group_unref(struct process_group* group);

struct thread* thread_create_kernel(uintptr_t entry, void* arg);
struct thread* thread_create_user(struct process* process, uintptr_t entry, uintptr_t stack);
void thread_destroy(struct thread* thread);
struct thread* thread_fork(struct process* process, struct thread* old_thread, struct registers* context);

void process_init(void);

#endif /* _KERNEL_SYS_PROCESS_H */
