#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h> 
#include <mem/slab.h>
#include <sys/process.h> 
#include <sys/timer.h> 
#include <types.h>
#include <utils/macros.h> 
#include <utils/usercopy.h> 
#include <utils/vector.h>

void sys_poll(struct registers* r) {
    struct pollfd* fds = (struct pollfd*) r->rdi;
    nfds_t nfds = r->rsi;
    const struct timespec* timeout = (const struct timespec*) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct pollfd* kfds = kmalloc(sizeof(struct pollfd) * nfds);
    if (unlikely(kfds)) {
        r->rax = -ENOMEM;
        return;
    }

    int ret;
    if ((ret = user_memcpy_from_user(kfds, fds, sizeof(struct pollfd) * nfds)) < 0) {
        kfree(kfds);
        r->rax = ret;
        return;
    }

    struct timespec ktimeout;
    if (timeout != NULL) {
        if ((ret = user_memcpy_from_user(&ktimeout, timeout, sizeof(struct timespec))) < 0) {
            kfree(kfds);
            r->rax = ret;
            return;
        }
    }

    int event_count = 0;

    vector_t* files = vector_create(sizeof(struct file*));
    if (unlikely(files == NULL)) {
        kfree(kfds);
        r->rax = -ENOMEM;
        return;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd* pollfd = &kfds[i];

        pollfd->revents = 0;
        if (pollfd->fd < 0) {
            continue;
        }

        struct file* file = file_get(current_process, pollfd->fd);
        if (file == NULL) {
            pollfd->revents = POLLNVAL;
            event_count++;
            continue;
        }

        if (!vector_push(files, &file)) {
            r->rax = -ENOMEM;
            goto end;
        }
    }

    // TODO: actually implement the polling

    for (size_t i = 0; i < vector_size(files); i++) {
        struct file* file = *vector_get(files, i);
        file_release(file);
    }

    ret = event_count;
end:
    vector_destroy(files);
    kfree(kfds);

    r->rax = ret;
}
