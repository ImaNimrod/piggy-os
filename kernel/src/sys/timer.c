#include <cpu/asm.h>
#include <cpu/smp.h>
#include <cpu/tsc.h>
#include <dev/cmos.h>
#include <dev/hpet.h>
#include <dev/pvclock.h>
#include <errno.h>
#include <sys/timer.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

#define NS_PER_S 1000000000ULL

struct timespec time_realtime;

static uint64_t last_ticks;
static struct timer_event* timer_event_list;
static spinlock_t timer_event_list_lock;

/*
 * Timer priority ranking is as follows:
 * 
 * 1. KVM pvclock
 * 2. Invariant TSC
 * 3. HPET
 *
 * The timing subsystem will select the best timer out of all available timers
 */
static struct timer_driver* timer_drivers[] = {
    &hpet_driver,
    &pvclock_driver,
    &tsc_driver,
};

struct timespec timer_time_from_boot(void) {
    uint64_t ticks = this_cpu()->timer_driver->ticks(this_cpu()->timer_info) - this_cpu()->timer_base_ticks + this_cpu()->timer_tick_offset;
    uint64_t hz = this_cpu()->timer_info->hz;
    if (hz == 0) {
        return (struct timespec) {};
    }

    time_t secs = ticks / hz;
    time_t rem = ticks % hz;
    time_t nsecs = (rem * NS_PER_S) / hz;

    return (struct timespec) { secs, nsecs };
}

int timer_remove(struct timer_event* event) {
    bool int_state = spinlock_acquire_irqsave(&timer_event_list_lock);
    SLIST_REMOVE(timer_event_list, event, next);
    spinlock_release_irqsave(&timer_event_list_lock, int_state);

    return 0;
}

int timer_setup(struct timer_event* event, timer_callback_t callback, void* arg, const struct timespec* tp) {
    event->callback = callback;
    event->arg = arg;
    event->ts = *tp;
    event->next = NULL;

    struct timespec boottime = timer_time_from_boot();
    timespec_add(&event->ts, &boottime);

    bool int_state = spinlock_acquire_irqsave(&timer_event_list_lock);
    SLIST_PUSH_FRONT(timer_event_list, event, next);
    spinlock_release_irqsave(&timer_event_list_lock, int_state);

    return 0;
}

void timer_update_timers(void) {
    if (unlikely(last_ticks == 0)) {
        last_ticks = this_cpu()->timer_base_ticks;
    }

    uint64_t current_ticks = this_cpu()->timer_driver->ticks(this_cpu()->timer_info);
    struct timespec interval = { 0, (current_ticks - last_ticks) * 1000000000 / this_cpu()->timer_info->hz };

    timespec_add(&time_realtime, &interval);

    last_ticks = current_ticks;

    struct timespec boottime = timer_time_from_boot();

    struct timer_event* expired = NULL;

    bool int_state = spinlock_acquire_irqsave(&timer_event_list_lock);

    struct timer_event* iter;
    struct timer_event* next;

    for (iter = timer_event_list; iter; iter = next) {
        next = iter->next;

        if (timespec_greater(&boottime, &iter->ts)) {
            SLIST_REMOVE(timer_event_list, iter, next);

            iter->next = expired;
            expired = iter;
        }
    }

    spinlock_release_irqsave(&timer_event_list_lock, int_state);

    while (expired) {
        struct timer_event* enext = expired->next;

        expired->callback(expired->arg);

        expired = enext;
    }
}

void timer_wait_ns(uint64_t ns) {
    uint64_t start_ticks = this_cpu()->timer_driver->ticks(this_cpu()->timer_info);
    uint64_t target_ticks = start_ticks + ((ns * this_cpu()->timer_info->hz) / NS_PER_S);

    while (this_cpu()->timer_driver->ticks(this_cpu()->timer_info) < target_ticks) {
        pause();
    }
}

void timer_early_percpu_init(void) {
    struct timer_driver* early_driver = NULL;
    int max_priority = -1;

    for (size_t i = 0; i < SIZEOF_ARRAY(timer_drivers); i++) {
        struct timer_driver* driver = timer_drivers[i];
        if (!driver->bootstrap) {
            continue;
        }

        if (max_priority < driver->priority && driver->check()) {
            early_driver = driver;
            max_priority = driver->priority;
        }
    }

    if (unlikely(!early_driver)) {
        kpanic(NULL, false, "failed to find early timer driver");
    }

    struct timer_info* early_info = early_driver->init();

    this_cpu()->timer_driver = early_driver;
    this_cpu()->timer_info = early_info;
    this_cpu()->timer_base_ticks = early_driver->ticks(early_info);
}

void timer_percpu_init(void) {
    struct timer_driver* old_driver = this_cpu()->timer_driver;
    struct timer_info* old_info = this_cpu()->timer_info;

    struct timer_driver* new_driver = old_driver;
    int max_priority = old_driver->priority;

    for (size_t i = 0; i < SIZEOF_ARRAY(timer_drivers); i++) {
        struct timer_driver* driver = timer_drivers[i];
        if (max_priority < driver->priority && driver->check()) {
            new_driver = driver;
            max_priority = driver->priority;
        }
    }

    if (new_driver == old_driver) {
        return;
    }

    uint64_t old_base_ticks = this_cpu()->timer_base_ticks;

    struct timer_info* new_info = new_driver->init();

    this_cpu()->timer_driver = new_driver;
    this_cpu()->timer_info = new_info;
    this_cpu()->timer_base_ticks = new_driver->ticks(new_info);

    uint64_t old_ticks = old_driver->ticks(old_info);
    uint64_t delta_ticks = old_ticks - old_base_ticks;

    uint64_t old_ns = (delta_ticks * NS_PER_S) / old_info->hz;

    this_cpu()->timer_tick_offset = (old_ns * new_info->hz) / NS_PER_S;
}
