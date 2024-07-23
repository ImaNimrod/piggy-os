#include <sys/syscall.h>
#include <unistd.h>

extern void __cleanup_stdio(void);
extern void _fini(void);

__attribute__((noreturn)) void _exit(int status) {
    _fini();
    __cleanup_stdio();
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}
