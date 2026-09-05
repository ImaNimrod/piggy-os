#include <cpu/smp.h>
#include <errno.h>
#include <mem/slab.h>
#include <sys/futex.h>
#include <sys/scheduler.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/usercopy.h>

static hashmap_t* futex_map;
static mutex_t futex_map_mutex;

static void timer_callback(void* arg) {
    struct thread* thread = arg;
    scheduler_wakeup(thread, -ETIMEDOUT);
}

int futex_wait(uintptr_t futex_paddr, uint32_t* addr, uint32_t value, const struct timespec* timeout) {
    mutex_acquire(&futex_map_mutex);

    uint32_t current_value;

    int ret = user_memcpy_from_user(&current_value, addr, sizeof(uint32_t));
    if (ret < 0) {
        mutex_release(&futex_map_mutex);
        return ret;
    }

    if (current_value != value) {
        mutex_release(&futex_map_mutex);
        return -EAGAIN;
    }

    struct futex* futex;

    if (!hashmap_get(futex_map, &futex_paddr, sizeof(uintptr_t), (void**) &futex)) {
        futex = kmalloc(sizeof(struct futex));
        if (unlikely(!futex)) {
            ret = -ENOMEM;
            goto end;
        }

        wait_queue_init(&futex->wq);
        futex->waiters = 0;

        if (!hashmap_set(futex_map, &futex_paddr, sizeof(uintptr_t), futex)) {
            kfree(futex);
            ret = -ENOMEM;
            goto end;
        }
    }

    futex->waiters++;

    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);
    wait_queue_add(&futex->wq, &this_cpu()->scheduler.current_thread->wait_node);

    struct timer_event event;
    bool timer_created = false;

    if (timeout) {
        ret = timer_setup(&event, timer_callback, this_cpu()->scheduler.current_thread, timeout);
        if (ret < 0) {
            futex->waiters--;

            if (futex->waiters == 0) {
                hashmap_remove(futex_map, &futex_paddr, sizeof(futex_paddr));
                kfree(futex);
            }

            goto end;
        }

        timer_created = true;
    }

    ret = scheduler_yield();

    if (timer_created) {
        timer_remove(&event);
    }

    futex->waiters--;

    if (futex->waiters == 0) {
        hashmap_remove(futex_map, &futex_paddr, sizeof(futex_paddr));
        kfree(futex);
    }

end:
    mutex_release(&futex_map_mutex);
    return ret;
}

int futex_wake(uintptr_t futex_paddr) {
    struct futex* futex;

    mutex_acquire(&futex_map_mutex);

    if (!hashmap_get(futex_map, &futex_paddr, sizeof(uintptr_t), (void**) &futex)) {
        mutex_release(&futex_map_mutex);
        return 0;
    }

    bool ret = wait_queue_wake_one(&futex->wq);

    mutex_release(&futex_map_mutex);
    return ret ? 1 : 0;
}

void futex_init(void) {
    futex_map = hashmap_create(256);
    if (unlikely(!futex_map)) {
        kpanic(NULL, false, "failed to create futex map");
    }

    mutex_init(&futex_map_mutex);
}
