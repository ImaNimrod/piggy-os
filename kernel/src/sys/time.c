#include <dev/pit.h>
#include <limine.h>
#include <sys/time.h>
#include <types.h> 
#include <utils/log.h>

#define TIMER_FREQUENCY 1000

static volatile struct limine_boot_time_request boot_time_request = {
    .id = LIMINE_BOOT_TIME_REQUEST,
    .revision = 0
};

struct timespec time_realtime = {0};
struct timespec time_monotonic = {0};

static inline struct timespec timespec_add(struct timespec a, struct timespec b) {
    if (a.tv_nsec + b.tv_nsec > 999999999) {
        a.tv_nsec = (a.tv_nsec + b.tv_nsec) - 1000000000;
        a.tv_sec++;
    } else {
        a.tv_nsec += b.tv_nsec;
    }
    a.tv_sec += b.tv_sec;
    return a;
}

static inline struct timespec timespec_sub(struct timespec a, struct timespec b) {
    if (b.tv_nsec > a.tv_nsec) {
        a.tv_nsec = 999999999 - (b.tv_nsec - a.tv_nsec);
        if (a.tv_sec == 0) {
            a.tv_sec = a.tv_nsec = 0;
            return a;
        }
        a.tv_sec--;
    } else {
        a.tv_nsec -= b.tv_nsec;
    }

    if (b.tv_sec > a.tv_sec) {
        a.tv_sec = a.tv_nsec = 0;
        return a;
    }

    a.tv_sec -= b.tv_sec;
    return a;
}

void time_update_timers(void) {
    struct timespec interval = {
        .tv_sec = 0,
        .tv_nsec = 1000000000 / TIMER_FREQUENCY
    };

    time_monotonic = timespec_add(time_monotonic, interval);
    time_realtime = timespec_add(time_realtime, interval);
}

void time_init(void) {
    struct limine_boot_time_response* boot_time_response = boot_time_request.response;

    time_realtime.tv_sec = boot_time_response->boot_time;

    pit_init(TIMER_FREQUENCY);
    klog("[time] initialized timing subsytem\n");
}
