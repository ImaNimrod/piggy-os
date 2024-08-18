#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

int usleep(unsigned int useconds) {
    const struct timespec duration = {
        .tv_sec = useconds / (1000000),
        .tv_nsec = (useconds % 1000000) * 1000
    };

    return (int) syscall1(SYS_SLEEP, (uint64_t) &duration);
}
