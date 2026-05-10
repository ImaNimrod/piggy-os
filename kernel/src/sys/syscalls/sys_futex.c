#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <stdint.h> 
#include <sys/process.h>
#include <utils/hashmap.h>
#include <utils/event.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/usercopy.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct futex {
    struct event event;
    int waiters;
};

static hashmap_t* futex_map;
static mutex_t futex_map_mutex;

static int futex_wait(struct futex* futex, uintptr_t paddr, uint32_t* addr, uint32_t value) {
    uint32_t current_value;

    int ret = 0;
    if ((ret = user_memcpy_from_user(&current_value, addr, sizeof(uint32_t))) < 0) {
        return ret;
    }

    if (current_value != value) {
        return -EAGAIN;
    }

    mutex_acquire(&futex_map_mutex);

    if (futex == NULL) {
        futex = kmalloc(sizeof(struct futex));
        if (futex == NULL) {
            ret = -ENOMEM;
            goto end;
        }

        event_init(&futex->event);

        if (!hashmap_set(futex_map, &paddr, sizeof(uintptr_t), futex)) {
            ret = -ENOMEM;
            goto end;
        }

    }

    futex->waiters++;
    mutex_release(&futex_map_mutex);

    event_wait(&futex->event, true);

    mutex_acquire(&futex_map_mutex);
    futex->waiters--;

    if (futex->waiters == 0) {
        hashmap_remove(futex_map, &paddr, sizeof(uintptr_t));
        kfree(futex);
    }

end:
    mutex_release(&futex_map_mutex);
    return ret;
}

static int futex_wake(struct futex* futex) {
    mutex_acquire(&futex_map_mutex);

    if (futex == NULL) {
        goto end;
    }

    event_trigger(&futex->event);

end:
    mutex_release(&futex_map_mutex);
    return 0;
}

void sys_futex(struct registers* r) {
    uint32_t* addr = (uint32_t*) r->rdi;
    int op = r->rsi;
    uint32_t value = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(addr)) {
        r->rax = -EFAULT;
        return;
    }

    if (unlikely(futex_map == NULL)) {
        futex_map = hashmap_create(512);
        if (futex_map == NULL) {
            r->rax = -ENOMEM;
            return;
        }

        mutex_init(&futex_map_mutex);
    }

    page_size_t unused;

    uintptr_t paddr = pagemap_get_mapping(current_process->vmm_context->pagemap, (uintptr_t) addr, &unused);
    if (paddr == 0) {
        r->rax = -EINVAL;
        return;
    }

    struct futex* futex = NULL;

    mutex_acquire(&futex_map_mutex);
    hashmap_get(futex_map, &paddr, sizeof(uintptr_t), (void**) &futex);
    mutex_release(&futex_map_mutex);

    switch (op) {
        case FUTEX_WAIT:
            r->rax = futex_wait(futex, paddr, addr, value);
            break;
        case FUTEX_WAKE:
            r->rax = futex_wake(futex);
            break;
        default:
            r->rax = -ENOSYS;
            break;
    }
}
