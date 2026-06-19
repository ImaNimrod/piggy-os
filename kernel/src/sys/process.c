#include <cpu/asm.h>
#include <cpu/smp.h>
#include <fs/devfs.h> 
#include <fs/procfs.h> 
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/cmdline.h>
#include <utils/macros.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/string.h>

struct process* kernel_process;
struct process* init_process;

hashmap_t* processes;
mutex_t processes_mutex;

static struct slab_cache* process_cache;
static struct slab_cache* process_group_cache;
static struct slab_cache* thread_cache;

static hashmap_t* process_groups;
static mutex_t process_groups_mutex;

static pid_t next_pid;

static char** dup_array(char** argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    char** new_argv = kmalloc((argc + 1) * sizeof(char*));
    if (unlikely(new_argv == NULL)) {
        return NULL;
    }

    for (int i = 0; i < argc; i++) {
        new_argv[i] = strdup(argv[i]);

        if (unlikely(new_argv[i] == NULL)) {
            while (i > 0) {
                kfree(new_argv[--i]);
            }

            kfree(new_argv);
            return NULL;
        }
    }

    new_argv[argc] = NULL;
    return new_argv;
}

static void free_array(char** argv) {
    for (size_t i = 0; argv[i] != NULL; i++) {
        kfree(argv[i]);
    }

    kfree(argv);
}

static void group_destroy(struct process_group* group) {
    mutex_acquire(&group->mutex);

    mutex_acquire(&process_groups_mutex);
    hashmap_remove(process_groups, &group->pgid, sizeof(pid_t));
    mutex_release(&process_groups_mutex);

    slab_cache_free(process_group_cache, group);
}

struct process* process_create(struct process* parent) {
    struct process* new_process = slab_cache_alloc(process_cache);
    if (unlikely(new_process == NULL)) {
        return NULL;
    }

    new_process->pid = __atomic_fetch_add(&next_pid, 1, __ATOMIC_SEQ_CST);
    new_process->state = PROCESS_STATE_RUNNING;

    spinlock_init(&new_process->thread_list_lock);

    new_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(new_process->threads == NULL)) {
        goto error;
    }

    spinlock_init(&new_process->node_lock);
    mutex_init(&new_process->fd_mutex);
    spinlock_init(&new_process->signal_actions_lock);

    if (likely(parent != NULL)) {
        new_process->cmdline = dup_array(parent->cmdline);
        if (unlikely(new_process->cmdline == NULL)) {
            goto error;
        }

        new_process->environ = dup_array(parent->environ);
        if (unlikely(new_process->environ == NULL)) {
            goto error;
        }

        spinlock_acquire(&parent->node_lock);

        new_process->cwd = parent->cwd;
        VFS_NODE_REF(new_process->cwd);
        new_process->root = parent->root;
        VFS_NODE_REF(new_process->root);

        spinlock_release(&parent->node_lock);

        file_fork(parent, new_process);

        new_process->vmm_context = vmm_context_fork(parent->vmm_context);
        if (unlikely(new_process->vmm_context == NULL)) {
            goto error;
        }

        spinlock_acquire(&parent->signal_actions_lock);
        memcpy(new_process->signal_actions, parent->signal_actions, sizeof(parent->signal_actions));
        spinlock_release(&parent->signal_actions_lock);

        new_process->parent = parent;
        SLIST_PUSH_FRONT(parent->children, new_process, sibling_next);

        process_group_add(parent->group, new_process);
    } else {
        new_process->cwd = vfs_root;
        VFS_NODE_REF(vfs_root);
        new_process->root = vfs_root;
        VFS_NODE_REF(vfs_root);

        new_process->vmm_context = vmm_context_create();
        if (unlikely(new_process->vmm_context == NULL)) {
            goto error;
        }

        if (unlikely(process_group_create(new_process) == NULL)) {
            goto error;
        }
    }

    mutex_acquire(&processes_mutex);
    hashmap_set(processes, &new_process->pid, sizeof(pid_t), new_process);
    mutex_release(&processes_mutex);

    return new_process;

error:
    if (new_process->cmdline != NULL) {
        free_array(new_process->cmdline);
    }
    if (new_process->environ != NULL) {
        free_array(new_process->environ);
    }

    if (new_process->vmm_context != NULL) {
        vmm_context_destroy(new_process->vmm_context);
    }

    if (new_process->threads != NULL) {
        vector_destroy(new_process->threads);
    }

    slab_cache_free(process_cache, new_process);
    return NULL;
}

void process_create_init(void) {
    char* init_path = cmdline_get("init");
    if (!init_path) {
        init_path = "/usr/bin/init";
    }

    klog("[process] starting init process %s\n", init_path);

    struct vfs_node* init_node;
    if (vfs_lookup(vfs_root, init_path, 0, NULL, &init_node) < 0) {
        kpanic(NULL, false, "failed to find %s", init_path);
    }
    init_node->ops->unlock(init_node);

    init_process = process_create(NULL);
    if (unlikely(init_process == NULL)) {
        kpanic(NULL, false, "failed to create init process");
    }

    struct vfs_node* console_node;
    if (devfs_get("console", &console_node) < 0) {
        kpanic(NULL, false, "failed to find console device");
    }
    console_node->ops->unlock(console_node);

    struct file* stdin_file = file_create(console_node, O_RDONLY);
    if (unlikely(stdin_file == NULL)) {
        kpanic(NULL, false, "failed to create stdin file descriptor for init process");
    }
    init_process->fds[0].file = stdin_file;
    struct file* stdout_file = file_create(console_node, O_WRONLY);
    if (unlikely(stdout_file == NULL)) {
        kpanic(NULL, false, "failed to create stdout file descriptor for init process");
    }
    init_process->fds[1].file = stdout_file;
    struct file* stderr_file = file_create(console_node, O_WRONLY);
    if (unlikely(stderr_file == NULL)) {
        kpanic(NULL, false, "failed to create stderr file descriptor for init process");
    }
    init_process->fds[2].file = stderr_file;

    char* argv[] = { init_path, NULL };
    char* envp[] = { NULL };

    char* ld_path = NULL;

    init_process->cmdline = kmalloc(2 * sizeof(char*));
    if (unlikely(init_process->cmdline == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for init process command line");
    }
    init_process->cmdline[0] = strdup(init_path);
    init_process->cmdline[1] = NULL;

    init_process->environ = kmalloc(1 * sizeof(char*));
    if (unlikely(init_process->environ == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for init process environment");
    }
    init_process->environ[0] = NULL;

    struct auxvals auxvals;
    if (elf_load(init_process->vmm_context, 0, init_node, &auxvals, &ld_path) < 0) {
        kpanic(NULL, false, "failed to load ELF for init process");
    }

    uintptr_t entry = auxvals.at_entry.value;

    if (ld_path != NULL) {
        struct vfs_node* ld_node;
        if (vfs_lookup(vfs_root, ld_path, 0, NULL, &ld_node) < 0) {
            kpanic(NULL, false, "failed to find interpreter %s", ld_node);
        }
        ld_node->ops->unlock(ld_node);

        struct auxvals ld_auxvals;
        if (elf_load(init_process->vmm_context, INTERPRETER_LOAD_BASE, ld_node, &ld_auxvals, NULL) < 0) {
            kpanic(NULL, false, "failed to load ELF for init process interpreter");
        }

        entry = ld_auxvals.at_entry.value;

        VFS_NODE_UNREF(ld_node);
        kfree(ld_path);
    }

    VFS_NODE_UNREF(init_node);

    uintptr_t user_stack_paddr = pmm_alloc(USER_STACK_SIZE / PAGE_SIZE_4KB);
    uintptr_t user_stack_vaddr = (uintptr_t) vmm_map(init_process->vmm_context, PROCESS_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, NULL, 0, user_stack_paddr);

    uintptr_t stack_top = elf_setup_stack(user_stack_vaddr + USER_STACK_SIZE, user_stack_paddr + USER_STACK_SIZE,
            init_path, argv, envp, &auxvals);

    struct thread* init_thread = thread_create_user(init_process, entry, stack_top);
    if (unlikely(init_thread == NULL)) {
        kpanic(NULL, false, "failed to create thread for init process");
    }
}

void process_destroy(struct process* process) {
    mutex_acquire(&processes_mutex);
    hashmap_remove(processes, &process->pid, sizeof(pid_t));
    mutex_release(&processes_mutex);

    procfs_delete_nodes(process->pid);

    free_array(process->cmdline);
    free_array(process->environ);

    for (size_t i = 0; i < vector_size(process->threads); i++) {
        thread_destroy((struct thread*) *vector_get(process->threads, i));
    }
    vector_destroy(process->threads);

    slab_cache_free(process_cache, process);

}

// TODO: finish locking process children list and restructring process / thread exit

void process_exit(struct process* process, int status) {
    if (unlikely(process->pid == 1)) {
        kpanic(NULL, false, "attempted to exit init process with status = %d", status);
    }

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        file_close(process, i);
    }

    VFS_NODE_UNREF(process->cwd);
    VFS_NODE_UNREF(process->root);

    SLIST_REMOVE(process->parent->children, process, sibling_next);

    // Reparent dying process' children to init

    struct process* child = process->children;
    while (child != NULL) {
        struct process* next = child->sibling_next;

        child->parent = init_process;
        SLIST_PUSH_FRONT(init_process->children, child, sibling_next);

        child = next;
    }

    signal_send_process(init_process, SIGCHLD);

    process_group_remove(process->group, process);

    vmm_context_destroy(process->vmm_context);

    process->state = PROCESS_STATE_ZOMBIE;
    process->exit_status = status;
}

struct process* process_find_by_pid(pid_t pid) {
    mutex_acquire(&processes_mutex);

    struct process* process = NULL;
    hashmap_get(processes, &pid, sizeof(pid_t), (void**) &process);

    mutex_release(&processes_mutex);
    return process;
}

struct vfs_node* process_get_cwd(struct process* process) {
    spinlock_acquire(&process->node_lock);
    struct vfs_node* cwd = process->cwd;
    VFS_NODE_REF(cwd);
    spinlock_release(&process->node_lock);
    return cwd;
}

struct vfs_node* process_get_root(struct process* process) {
    spinlock_acquire(&process->node_lock);
    struct vfs_node* root = process->root;
    VFS_NODE_REF(root);
    spinlock_release(&process->node_lock);
    return root;
}

void process_set_cwd(struct process* process, struct vfs_node* new_cwd) {
    spinlock_acquire(&process->node_lock);
    struct vfs_node* old_cwd = process->cwd;

    VFS_NODE_REF(new_cwd);
    process->cwd = new_cwd;

    VFS_NODE_UNREF(old_cwd);
    spinlock_release(&process->node_lock);
}

void process_set_root(struct process* process, struct vfs_node* new_root) {
    spinlock_acquire(&process->node_lock);
    struct vfs_node* old_root = process->root;

    process->root = new_root;
    VFS_NODE_REF(new_root);

    VFS_NODE_UNREF(old_root);
    spinlock_release(&process->node_lock);
}

struct process_group* process_group_create(struct process* leader) {
    struct process_group* group = slab_cache_alloc(process_group_cache);
    if (unlikely(group == NULL)) {
        return NULL;
    }

    mutex_init(&group->mutex);

    group->pgid = leader->pid;
    group->head = leader;

    mutex_acquire(&process_groups_mutex);

    if (!hashmap_set(process_groups, &group->pgid, sizeof(pid_t), group)) {
        mutex_release(&process_groups_mutex);
        slab_cache_free(process_group_cache, group);
        return NULL;
    }

    leader->group = group;

    mutex_release(&process_groups_mutex);
    return group;
}

void process_group_add(struct process_group* group, struct process* process) {
    mutex_acquire(&group->mutex);
    DLIST_PUSH_FRONT(group->head, process, group_prev, group_next);
    process->group = group;
    mutex_release(&group->mutex);
}

struct process_group* process_group_find_by_pgid(pid_t pgid) {
    mutex_acquire(&process_groups_mutex);

    struct process_group* group = NULL;
    hashmap_get(process_groups, &pgid, sizeof(pid_t), (void**) &group);

    mutex_release(&process_groups_mutex);
    return group;
}

void process_group_move(struct process_group* new_group, struct process* process) {
    struct process_group* old_group = process->group;

    if (old_group == new_group) {
        return;
    }

    if (old_group == NULL) {
        process_group_add(new_group, process);
        return;
    }

    mutex_acquire(&old_group->mutex);
    mutex_acquire(&new_group->mutex);

    DLIST_REMOVE(old_group->head, process, group_prev, group_next);
    DLIST_PUSH_FRONT(new_group->head, process, group_prev, group_next);

    bool old_empty = DLIST_IS_EMPTY(old_group->head);

    mutex_release(&new_group->mutex);
    mutex_release(&old_group->mutex);

    if (old_empty) {
        group_destroy(old_group);
    }
}

void process_group_remove(struct process_group* group, struct process* process) {
    mutex_acquire(&group->mutex);

    DLIST_REMOVE(group->head, process, group_prev, group_next);
    process->group = NULL;

    bool empty = DLIST_IS_EMPTY(group->head);

    mutex_release(&group->mutex);

    if (empty) {
        group_destroy(group);
    }
}

struct thread* thread_create_kernel(uintptr_t entry, void* arg) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    // Basic state
    thread->cpu = this_cpu();
    thread->process = kernel_process;

    spinlock_init(&thread->state_lock);

    thread->state = THREAD_STATE_INIT;
    thread->flags = 0;

    thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    thread->kernel_stack = thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    // CPU context
    thread->registers.rdi = (uint64_t) arg;
    thread->registers.rip = entry;
    thread->registers.cs = KERNEL_CODE_SEGMENT;
    thread->registers.rflags = 0x202;
    thread->registers.ss = KERNEL_DATA_SEGMENT;
    thread->registers.rsp = thread->kernel_stack;

    // Insert into process' thread list
    spinlock_acquire(&kernel_process->thread_list_lock);

    thread->tid = vector_size(kernel_process->threads);
    vector_push(kernel_process->threads, &thread);

    spinlock_release(&kernel_process->thread_list_lock);

    scheduler_enqueue(&thread->cpu->scheduler, thread);
    return thread;
}

struct thread* thread_create_user(struct process* process, uintptr_t entry, uintptr_t stack) {
    struct thread* thread = slab_cache_alloc(thread_cache);
    if (unlikely(thread == NULL)) {
        return NULL;
    }

    // Basic state
    thread->cpu = this_cpu();
    thread->process = process;

    spinlock_init(&thread->state_lock);

    thread->state = THREAD_STATE_INIT;
    thread->flags = THREAD_FLAG_USER;

    thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    thread->kernel_stack = thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    // CPU context
    thread->registers.rip = entry;
    thread->registers.cs = USER_CODE_SEGMENT;
    thread->registers.rflags = 0x202;
    thread->registers.ss = USER_DATA_SEGMENT;
    thread->registers.rsp = stack;

    thread->fpu_context = (void*) (pmm_alloc_zero(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    ((uint16_t*) thread->fpu_context)[0] = DEFAULT_FCW;
    ((uint32_t*) thread->fpu_context)[6] = DEFAULT_MXCSR;

    thread->fs_base = 0;
    thread->gs_base = 0;

    // Signals
    spinlock_init(&thread->signal_lock);
    thread->pending_signals = thread->signal_mask = 0;
    thread->signal_stack.ss_flags = SS_DISABLE;

    // Insert into process' thread list
    spinlock_acquire(&process->thread_list_lock);

    thread->tid = vector_size(process->threads);
    vector_push(process->threads, &thread);

    spinlock_release(&process->thread_list_lock);

    scheduler_enqueue(&thread->cpu->scheduler, thread);
    return thread;
}

void thread_destroy(struct thread* thread) {
    if (thread->flags & THREAD_FLAG_USER) {
        pmm_free((uintptr_t) thread->fpu_context - HIGH_VMA, DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB));
        // User stack physical pages are already freed during vmm_context_destroy called in process_exit
    }

    pmm_free(thread->kernel_stack_paddr, KERNEL_STACK_SIZE / PAGE_SIZE_4KB);

    slab_cache_free(thread_cache, thread);
}

struct thread* thread_fork(struct process* process, struct thread* old_thread, struct registers* context) {
    struct thread* new_thread = slab_cache_alloc(thread_cache);
    if (unlikely(new_thread == NULL)) {
        return NULL;
    }

    // Basic state
    new_thread->cpu = this_cpu();
    new_thread->process = process;

    spinlock_init(&new_thread->state_lock);

    new_thread->state = THREAD_STATE_INIT;
    new_thread->flags = old_thread->flags;

    new_thread->kernel_stack_paddr = pmm_alloc(KERNEL_STACK_SIZE / PAGE_SIZE_4KB);
    new_thread->kernel_stack = new_thread->kernel_stack_paddr + HIGH_VMA + KERNEL_STACK_SIZE;

    // CPU context
    memcpy64((uint64_t*) &new_thread->registers, (const uint64_t*) context, sizeof(struct registers) >> 3);
    new_thread->registers.rax = 0;

    new_thread->fpu_context = (void*) (pmm_alloc(DIV_CEIL(this_cpu()->fpu_context_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    memcpy(new_thread->fpu_context, old_thread->fpu_context, this_cpu()->fpu_context_size);

    new_thread->fs_base = rdmsr(MSR_IA32_FS_BASE);
    new_thread->gs_base = rdmsr(MSR_IA32_KERNEL_GS_BASE);

    // Signals
    spinlock_init(&new_thread->signal_lock);

    spinlock_acquire(&old_thread->signal_lock);

    new_thread->pending_signals =  0;
    new_thread->signal_mask = old_thread->signal_mask;
    new_thread->signal_stack = old_thread->signal_stack;
    new_thread->signal_stack.ss_flags &= ~SS_ONSTACK;

    spinlock_release(&old_thread->signal_lock);

    // Insert into process' thread list
    spinlock_acquire(&process->thread_list_lock);

    new_thread->tid = vector_size(process->threads);
    vector_push(process->threads, &new_thread);

    spinlock_release(&process->thread_list_lock);

    scheduler_enqueue(&new_thread->cpu->scheduler, new_thread);
    return new_thread;
}

void process_init(void) {
    process_cache = slab_cache_create("struct process cache", sizeof(struct process));
    if (unlikely(process_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for process structs");
    }

    process_group_cache = slab_cache_create("struct process_group cache", sizeof(struct process_group));
    if (unlikely(process_group_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for process_group structs");
    }

    thread_cache = slab_cache_create("struct thread cache", sizeof(struct thread));
    if (unlikely(thread_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for thread structs");
    }

    mutex_init(&processes_mutex);

    processes = hashmap_create(128);
    if (unlikely(processes == NULL)) {
        kpanic(NULL, false, "failed to create process hashmap");
    }

    mutex_init(&process_groups_mutex);

    process_groups = hashmap_create(64);
    if (unlikely(process_groups == NULL)) {
        kpanic(NULL, false, "failed to create process group hashmap");
    }

    kernel_process = slab_cache_alloc(process_cache);
    if (unlikely(kernel_process == NULL)) {
        kpanic(NULL, false, "failed to create kernel process");
    }

    spinlock_init(&kernel_process->thread_list_lock);

    kernel_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(kernel_process->threads == NULL)) {
        kpanic(NULL, false, "failed to create kernel process threads vector");
    }

    kernel_process->pid = next_pid++;
    kernel_process->state = PROCESS_STATE_RUNNING;

    klog("[process] initialized kernel process\n");
}
