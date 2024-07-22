#include <sys/syscall.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int* status, int flags) {
    return syscall3(SYS_WAIT, pid, (uint64_t) status, flags);
}
