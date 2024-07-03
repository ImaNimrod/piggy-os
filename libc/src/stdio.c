#include <stdio.h>
#include <sys/syscalls.h>

int puts(const char* str) {
    syscall1(SYS_DEBUG, (uint64_t) str);
    return 0;
}
