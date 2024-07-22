#include <sys/syscall.h>
#include <unistd.h>

pid_t getppid(void) {
    return syscall0(SYS_GETPPID);
}
