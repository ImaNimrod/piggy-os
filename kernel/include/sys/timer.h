#ifndef _KERNEL_SYS_TIMER_H
#define _KERNEL_SYS_TIMER_H

#include <types.h>

#define CLOCK_REALTIME              0
#define CLOCK_MONOTONIC             1
#define CLOCK_PROCESS_CPUTIME_ID    2
#define CLOCK_THREAD_CPUTIME_ID     3
#define CLOCK_BOOTTIME              7

struct thread;

struct timer_info {
    uint64_t hz;
    void* private;
};

struct timer_driver {
    const char* name;
    int priority;
    bool bootstrap;

    bool (*check)(void);
    struct timer_info* (*init)(void);
    uint64_t (*ticks)(struct timer_info*);
};

typedef void (*timer_callback_t)(void*);

extern struct timespec time_realtime;

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

struct timespec timer_time_from_boot(void);
int timer_setup(timer_callback_t callback, void* arg, const struct timespec* tp);
void timer_update_timers(void);
void timer_wait_ns(uint64_t ns);

void timer_early_percpu_init(void);
void timer_percpu_init(void);

#endif /* _KERNEL_SYS_TIMER_H */
