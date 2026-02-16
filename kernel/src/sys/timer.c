#include <cpu/asm.h>
#include <cpu/smp.h>
#include <cpu/tsc.h>
#include <dev/cmos.h>
#include <dev/hpet.h>
#include <dev/pvclock.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <sys/scheduler.h>
#include <sys/timer.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

#define NS_PER_S 1000000000

struct sleep_event {
    struct thread* thread;
    struct timespec ts;
    struct sleep_event* next;
};

struct timespec time_monotonic;
struct timespec time_realtime;

static uint64_t last_ticks;
static struct sleep_event* sleep_event_list;
static spinlock_t sleep_event_list_lock;

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
    if (last_ticks == 0) {
        last_ticks = this_cpu()->timer_base_ticks;
    }

    uint64_t current_ticks = this_cpu()->timer_driver->ticks(this_cpu()->timer_info);
    struct timespec interval = { 0, (current_ticks - last_ticks) * 1000000000 / this_cpu()->timer_info->hz };

    timespec_add(&time_monotonic, &interval);
    timespec_add(&time_realtime, &interval);

    last_ticks = current_ticks;

    spinlock_acquire(&sleep_event_list_lock);

    struct sleep_event* iter;
    SLIST_FOREACH(sleep_event_list, iter) {
        if (timespec_greater(&time_monotonic, &iter->ts)) {
            struct thread* thread = iter->thread;
            SLIST_REMOVE(sleep_event_list, iter);
            kfree(iter);
            scheduler_unblock(thread);
        }
    }

    spinlock_release(&sleep_event_list_lock);
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

    if (unlikely(early_driver == NULL)) {
        kpanic(NULL, false, "failed to find early timer driver");
    }

    struct timer_info* early_info = early_driver->init();

    this_cpu()->timer_driver = early_driver;
    this_cpu()->timer_info = early_info;
    this_cpu()->timer_base_ticks = early_driver->ticks(early_info);

    klog("[timer] CPU #%zu selected %s as early timer driver\n",
            this_cpu()->cpu_number, early_driver->name);
}

void timer_percpu_init(void) {
    struct timer_driver* timer_driver = this_cpu()->timer_driver;
    int max_priority = this_cpu()->timer_driver->priority;

    for (size_t i = 0; i < SIZEOF_ARRAY(timer_drivers); i++) {
        struct timer_driver* driver = timer_drivers[i];
        if (max_priority < driver->priority && driver->check()) {
            timer_driver = driver;
            max_priority = driver->priority;
        }
    }

    if (timer_driver == this_cpu()->timer_driver) {
        return;
    }

    struct timer_info* timer_info = timer_driver->init();

    this_cpu()->timer_driver = timer_driver;
    this_cpu()->timer_info = timer_info;
    this_cpu()->timer_base_ticks = timer_driver->ticks(timer_info);

    klog("[timer] CPU #%zu switching to %s for timer driver\n",
            this_cpu()->cpu_number, timer_driver->name);
}

void timer_init(void) {
    cmos_init();
    time_realtime.tv_sec = cmos_get_rtc_timestamp();
}
