#include <sys/syscall.h>
#include <unistd.h>

int execve(const char* path, char* const argv[], char* const envp[]) {
    return syscall3(SYS_EXEC, (uint64_t) path, (uint64_t) argv, (uint64_t) envp);
}
