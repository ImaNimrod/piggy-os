#include <dev/cmos.h>
#include <dev/hpet.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <sys/timer.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

#define TIMER_FREQUENCY 250

struct sleep_event {
    struct thread* thread;
    struct timespec ts;
    struct sleep_event* next;
};

struct timespec time_monotonic = {0};
struct timespec time_realtime = {0};

static struct sleep_event* sleep_event_list = NULL;
static spinlock_t sleep_event_list_lock = {0};

static inline void timespec_add(struct timespec* a, const struct timespec* b) {
    if (a->tv_nsec + b->tv_nsec > 999999999) {
        a->tv_nsec = (a->tv_nsec + b->tv_nsec) - 1000000000;
        a->tv_sec++;
    } else {
        a->tv_nsec += b->tv_nsec;
    }

    a->tv_sec += b->tv_sec;
}

static inline bool timespec_greater(const struct timespec* a, const struct timespec* b) {
    if (a->tv_sec > b->tv_sec) {
        return true;
    } else if (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec) {
        return true;
    }

    return false;
}

void timer_sleep_thread(struct thread* thread, const struct timespec* tp) {
    struct sleep_event* event = kmalloc(sizeof(struct sleep_event));
    if (unlikely(event == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for sleep event");
    }

    event->thread = thread;
    event->ts = *tp;
    timespec_add(&event->ts, &time_monotonic);

    spinlock_acquire(&sleep_event_list_lock);
    SLIST_PUSH_BACK(sleep_event_list, event);
    spinlock_release(&sleep_event_list_lock);
}

void timer_update_timers(void) {
    struct timespec interval = {
        .tv_sec = 0,
        .tv_nsec = 1000000000 / TIMER_FREQUENCY
    };

    timespec_add(&time_monotonic, &interval);
    timespec_add(&time_realtime, &interval);

    spinlock_acquire(&sleep_event_list_lock);

    struct sleep_event* iter;
    SLIST_FOREACH(sleep_event_list, iter) {
        if (timespec_greater(&time_monotonic, &iter->ts)) {
            struct thread* thread = iter->thread;
            SLIST_REMOVE(sleep_event_list, iter);
            kfree(iter);
            scheduler_thread_unblock(thread);
        }
    }

    spinlock_release(&sleep_event_list_lock);
}

void timer_init(void) {
    hpet_init(TIMER_FREQUENCY);

    cmos_init();
    cmos_get_rtc_time(&time_realtime);

    klog("[timer] initialized timing subsystem\n");
}
