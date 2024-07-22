#include <sys/syscall.h>
#include <unistd.h>

__attribute__((noreturn)) void _exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}
