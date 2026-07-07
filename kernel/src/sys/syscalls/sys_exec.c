#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <sys/signal.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

#define SHEBANG_MAX_DEPTH  8
#define SHEBANG_MAX_LENGTH 128

static void free_string_array(char** xs) {
    for (char** iter = xs; *iter; iter++) {
        kfree(*iter);
    }
    kfree(xs);
}

static int exec_internal(char* path, int argc, char** argv, char** envp, size_t depth) {
    if (depth > SHEBANG_MAX_DEPTH) {
        return -ELOOP;
    }

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    int ret = 0;

    struct vfs_node* reference = NULL;
    struct vfs_node* node = NULL;
    char* ld_path = NULL;
    struct vfs_node* ld_node = NULL;

    struct vmm_context* old_vmm_context = current_process->vmm_context;
    struct vmm_context* new_vmm_context = vmm_context_create();
    if (unlikely(!new_vmm_context)) {
        ret = -ENOMEM;
        goto end;
    }

    reference = path[0] == '/' ? process_get_root(current_process) : process_get_cwd(current_process);

    if ((ret = vfs_lookup(reference, path, 0, NULL, &node)) < 0) {
        goto end;
    }
    node->ops->unlock(node);

    struct auxvals auxvals;

    ret = elf_load(new_vmm_context, 0, node, &auxvals, &ld_path);
    if (ret == -ENOEXEC) {
        char shebang[SHEBANG_MAX_LENGTH];

        node->ops->lock(node);
        ssize_t nread = node->ops->read(node, shebang, SHEBANG_MAX_LENGTH - 1, 0, 0);
        node->ops->unlock(node);

        if (nread < 2 || shebang[0] != '#' || shebang[1] != '!') {
            goto end;
        }

        shebang[nread] = '\0';

        for (ssize_t i = 2; i < nread; i++) {
            if (shebang[i] == '\n') {
                shebang[i] = '\0';
            }
        }

        char* command = shebang + 2;
        while (*command == ' ') {
            command++;
        }

        char* argument = command;
        while (*argument != ' ' && *argument != '\0') {
            argument++;
        }

        if (*argument == ' ') {
            *argument++ = '\0';
        }

        while (*argument == ' ') {
            argument++;
        }

        if (*command == '\0') {
            goto end;
        }

        char* interp = command;
        char* interp_arg = argument;

        char** old_argv = argv;

        int new_argc = argc + 1 + (*interp_arg ? 2 : 1);
        char** new_argv = kmallocz((new_argc + 1) * sizeof(char*));

        int j = 0;
        new_argv[j++] = strdup(interp);

        if (*interp_arg) {
            new_argv[j++] = strdup(interp_arg);
        }

        new_argv[j++] = strdup(path);

        for (int k = 1; argv[k]; k++) {
            new_argv[j++] = strdup(argv[k]);
        }
        new_argv[j] = NULL;

        free_string_array(old_argv);
        argv = new_argv;

        ret = exec_internal(interp, new_argc, argv, envp, depth + 1);
        goto end;
    } else if (ret < 0) {
        goto end;
    }

    uintptr_t entry = auxvals.at_entry.value;

    if (ld_path) {
        if (vfs_lookup(vfs_root, ld_path, 0, NULL, &ld_node) < 0) {
            goto end;
        }
        ld_node->ops->unlock(ld_node);

        struct auxvals ld_auxvals;
        if ((ret = elf_load(new_vmm_context, INTERPRETER_LOAD_BASE, ld_node, &ld_auxvals, NULL)) < 0) {
            goto end;
        }

        entry = ld_auxvals.at_entry.value;
    }

    cli(); // no going back after this point

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        struct file_descriptor* descriptor = &current_process->fds[i];
        if (descriptor->file && descriptor->cloexec) {
            file_close(current_process, i);
        }
    }

    current_process->vmm_context = new_vmm_context;

    spinlock_acquire(&current_process->signal_actions_lock);

    for (size_t i = 0; i < SIZEOF_ARRAY(current_process->signal_actions); i++) {
        if (current_process->signal_actions[i].sa_handler != SIG_IGN) {
            current_process->signal_actions[i].sa_handler = SIG_DFL;
        }
    }

    spinlock_release(&current_process->signal_actions_lock);

    struct thread* t;
    for (size_t i = 0; i < vector_size(current_process->threads); i++) {
        t = *vector_get(current_process->threads, i);
        if (t != current_thread) {
            scheduler_dequeue(&t->cpu->scheduler, t);
            thread_destroy(t);
        }
    }
    vector_destroy(current_process->threads);

    current_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(!current_process->threads)) {
        ret = -ENOMEM;
        goto end;
    }

    uintptr_t user_stack_paddr = pmm_alloc(USER_STACK_SIZE / PAGE_SIZE_4KB);
    uintptr_t user_stack_vaddr = (uintptr_t) vmm_map(current_process->vmm_context, PROCESS_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, NULL, 0, user_stack_paddr);

    uintptr_t stack_top = elf_setup_stack(user_stack_vaddr + USER_STACK_SIZE, user_stack_paddr + USER_STACK_SIZE,
            path, argv, envp, &auxvals);

    struct thread* new_thread = thread_create_user(current_process, entry, stack_top);
    if (unlikely(!new_thread)) {
        ret = -ENOMEM;
        goto end;
    }

    spinlock_acquire(&current_thread->signal_lock);
    new_thread->pending_signals = current_thread->pending_signals;
    new_thread->signal_mask = current_thread->signal_mask;
    spinlock_release(&current_thread->signal_lock);

end:
    if (reference) {
        VFS_NODE_UNREF(reference);
    }
    if (node) {
        VFS_NODE_UNREF(node);
    }

    if (ld_path) {
        kfree(ld_path);
    }
    if (ld_node) {
        VFS_NODE_UNREF(ld_node);
    }

    if (ret < 0) {
        goto error;
    }

    if (depth == 0) {
        kfree(path);
    }

    free_string_array(current_process->cmdline);
    free_string_array(current_process->environ);

    current_process->cmdline = argv;
    current_process->environ = envp;

    pagemap_load(kernel_pagemap);
    vmm_context_destroy(old_vmm_context);

    scheduler_thread_exit();
    __builtin_unreachable();

error:
    if (current_process->vmm_context == old_vmm_context) {
        if (new_vmm_context) {
            vmm_context_destroy(new_vmm_context);
        }
    } else {
        process_exit(current_process, -1);
    }

    return ret;
}

void sys_exec(struct registers* r) {
    const char* path = (const char*) r->rdi;
    const char** argv = (const char**) r->rsi;
    const char** envp = (const char**) r->rdx;

    if (!IS_USER_ADDRESS(path) || !IS_USER_ADDRESS(argv) || !IS_USER_ADDRESS(envp)) {
        r->rax = -EFAULT;
        return;
    }

    int ret = 0;

    size_t path_len;
    if ((ret = user_strlen(path, &path_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* kpath = kmalloc(path_len + 1);
    if (unlikely(!kpath)) {
        r->rax = -ENOMEM;
        return;
    }
    kpath[path_len] = '\0';

    if ((ret = user_memcpy_from_user(kpath, path, path_len + 1)) < 0) {
        kfree(kpath);
        r->rax = ret;
        return;
    }

    int argc = 0;
    int envc = 0;
    char** kargv = NULL;
    char** kenvp = NULL;

    for (;;) {
        char* arg;
        ret = user_memcpy_from_user(&arg, &argv[argc], sizeof(char*));
        argc++;

        if (ret < 0) {
            goto end;
        }
        if (!arg) {
            break;
        }
    }

    for (;;) {
        char* env;
        ret = user_memcpy_from_user(&env, &envp[envc], sizeof(char*));
        envc++;

        if (ret < 0) {
            goto end;
        }
        if (!env) {
            break;
        }
    }

    kargv = kmallocz((argc + 1) * sizeof(char*));
    kenvp = kmallocz((envc + 1) * sizeof(char*));
    if (unlikely(!kargv || !kenvp)) {
        ret = -ENOMEM;
        goto end;
    }

    for (int i = 0; i < argc - 1; i++) {
        char* arg;
        if ((ret = user_memcpy_from_user(&arg, &argv[i], sizeof(char*))) < 0) {
            goto end;
        }

        size_t len;
        if ((ret = user_strlen(arg, &len)) < 0) {
            goto end;
        }

        kargv[i] = kmalloc(len + 1);
        if (!kargv[i]) {
            ret = -ENOMEM;
            goto end;
        }
        kargv[i][len] = '\0';

        if ((ret = user_memcpy_from_user(kargv[i], arg, len + 1)) < 0) {
            goto end;
        }
    }

    for (int i = 0; i < envc - 1; i++) {
        char* env;
        if ((ret = user_memcpy_from_user(&env, &envp[i], sizeof(char*))) < 0) {
            goto end;
        }

        size_t len;
        if ((ret = user_strlen(env, &len)) < 0) {
            goto end;
        }

        kenvp[i] = kmalloc(len + 1);
        if (!kenvp[i]) {
            ret = -ENOMEM;
            goto end;
        }
        kenvp[i][len] = '\0';

        if ((ret = user_memcpy_from_user(kenvp[i], env, len + 1)) < 0) {
            goto end;
        }
    }

    r->rax = exec_internal(kpath, argc, kargv, kenvp, 0);
    return;

end:
    kfree(kpath);

    if (kargv) {
        free_string_array(kargv);
    }
    if (kenvp) {
        free_string_array(kenvp);
    }

    r->rax = ret;
}
