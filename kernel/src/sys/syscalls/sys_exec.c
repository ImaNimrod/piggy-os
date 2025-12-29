#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/elf.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

static void free_string_array(char** xs) {
    char** iter = xs;
    while (*iter != NULL) {
        kfree(*iter++);
    }
    kfree(xs);
}

void sys_exec(struct registers* r) {
    const char* path = (const char*) r->rdi;
    const char** argv = (const char**) r->rsi;
    const char** envp = (const char**) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

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
    if (unlikely(kpath == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    if ((ret = user_memcpy_from_user(kpath, path, path_len)) < 0) {
        kfree(kpath);
        r->rax = ret;
        return;
    }

    int argc = 0;
    int envc = 0;
    char** kargv = NULL;
    char** kenvp = NULL;
    struct vmm_context* old_vmm_context = current_process->vmm_context;
    struct vmm_context* new_vmm_context = NULL;
    struct vfs_node* node = NULL;
    struct vfs_node* reference = NULL;
    struct vfs_node* ld_node = NULL;
    char* ld_path = NULL;

    for (;;) {
        char* arg;
        ret = user_memcpy_from_user(&arg, &argv[argc], sizeof(char*));
        argc++;

        if (ret < 0) {
            goto end;
        }
        if (arg == NULL) {
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
        if (env == NULL) {
            break;
        }
    }

    kargv = kmalloc((argc + 1) * sizeof(char*));
    kenvp = kmalloc((envc + 1) * sizeof(char*));
    if (unlikely(kargv == NULL || kenvp == NULL)) {
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
        if (kargv[i] == NULL) {
            ret = -ENOMEM;
            goto end;
        }

        if ((ret = user_memcpy_from_user(kargv[i], arg, len)) < 0) {
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
        if (kenvp[i] == NULL) {
            ret = -ENOMEM;
            goto end;
        }

        if ((ret = user_memcpy_from_user(kenvp[i], env, len)) < 0) {
            goto end;
        }
    }

    new_vmm_context = vmm_context_create();
    if (unlikely(new_vmm_context == NULL)) {
        ret = -ENOMEM;
        goto end;
    }

    reference = kpath[0] == '/' ? process_get_root(current_process) : process_get_cwd(current_process);

    if ((ret = vfs_lookup(reference, kpath, false, NULL, &node)) < 0) {
        goto end;
    }
    node->ops->unlock(node);

    struct auxvals auxvals;
    if ((ret = elf_load(new_vmm_context, 0, node, &auxvals, &ld_path)) < 0) {
        goto end;
    }

    uintptr_t entry = auxvals.at_entry.value;

    if (ld_path != NULL) {
        if (vfs_lookup(vfs_root, ld_path, false, NULL, &ld_node) < 0) {
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
        if (descriptor->file != NULL && descriptor->cloexec) {
            file_release(descriptor->file);
        }
    }

    current_process->vmm_context = new_vmm_context;
    current_process->thread_stack_top = PROCESS_STACK_TOP;

    struct thread* t;
    for (size_t i = 0; i < vector_size(current_process->threads); i++) {
        t = *vector_get(current_process->threads, i);
        if (t != current_thread) {
            scheduler_dequeue(t);
            thread_destroy(t);
        }
    }
    vector_destroy(current_process->threads);

    current_process->threads = vector_create(sizeof(struct thread*));
    if (unlikely(current_process->threads == NULL)) {
        ret = -ENOMEM;
        goto end;
    }

    struct thread* new_thread = thread_create_user(current_process, entry);
    if (unlikely(new_thread == NULL)) {
        ret = -ENOMEM;
        goto end;
    }

    elf_setup_stack(new_thread, new_thread->user_stack_paddr + USER_STACK_SIZE, kpath, kargv, kenvp, &auxvals);

    scheduler_enqueue(new_thread);

end:
    kfree(kpath);
    if (kargv != NULL) {
        free_string_array(kargv);
    }
    if (kenvp != NULL) {
        free_string_array(kenvp);
    }

    if (reference != NULL) {
        VFS_NODE_UNREF(reference);
    }
    if (node != NULL) {
        VFS_NODE_UNREF(node);
    }

    if (ld_path != NULL) {
        kfree(ld_path);
    }
    if (ld_node != NULL) {
        VFS_NODE_UNREF(ld_node);
    }

    if (ret < 0) {
        goto error;
    }

    pagemap_load(kernel_pagemap);

    scheduler_dequeue(current_thread);
    thread_destroy(current_thread);
    vmm_context_destroy(old_vmm_context);

    scheduler_yield(false);
    __builtin_unreachable();

error:
    if (current_process->vmm_context == old_vmm_context) {
        if (new_vmm_context != NULL) {
            vmm_context_destroy(new_vmm_context);
        }
    } else {
        process_exit(current_process, -1);
    }

    r->rax = ret;
}
