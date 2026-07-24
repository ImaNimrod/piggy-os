#include <cpu/smp.h>
#include <errno.h>
#include <fs/poll.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/macros.h>

static void timer_callback(void* arg) {
    struct thread* thread = arg;
    scheduler_wakeup(thread, -ETIMEDOUT);
}

int poll_table_init(struct poll_table* pt) {
    pt->entries = vector_create(sizeof(struct poll_table_entry));
    if (unlikely(!pt->entries)) {
        return -ENOMEM;
    }

    return 0;
}

void poll_table_deinit(struct poll_table* pt) {
    vector_destroy(pt->entries);
}

int poll_table_reset(struct poll_table* pt) {
    return vector_resize(pt->entries, 0) ? 0 : -ENOMEM;
}

int poll_table_add(struct poll_table* pt, struct wait_queue* wq) {
    struct poll_table_entry entry = {
        .node = {},
        .wq = wq,
    };

    if (unlikely(!vector_push(pt->entries, &entry))) {
        return -ENOMEM;
    }

    return 0;
}

int poll_table_wait(struct poll_table* pt, const struct timespec* timeout) {
    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);

    struct timer_event event;

    if (timeout) {
        int ret = timer_setup(&event, timer_callback, this_cpu()->scheduler.current_thread, timeout);
        if (ret < 0) {
            return ret;
        }
    }

    for (size_t i = 0; i < vector_size(pt->entries); i++) {
        struct poll_table_entry* entry = (struct poll_table_entry*) vector_get(pt->entries, i);
        wait_queue_add(entry->wq, &entry->node);
    }

    int ret = scheduler_yield();

    if (timeout) {
        timer_remove(&event);
    }

    return ret;
}
