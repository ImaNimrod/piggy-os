#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

int sleep(unsigned int seconds) {
    const struct timespec duration = {
        .tv_sec = seconds,
        .tv_nsec = 0
    };

    return (int) syscall1(SYS_SLEEP, (uint64_t) &duration);
}
