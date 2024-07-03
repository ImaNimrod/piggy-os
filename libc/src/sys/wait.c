#include <sys/syscalls.h>
#include <sys/wait.h>

pid_t wait(int* status) {
    return waitpid(-1, status, 0);
}

pid_t waitpid(pid_t pid, int* status, int flags) {
    return syscall3(SYS_WAIT, pid, (uint64_t) status, flags);
}
