#include <sys/syscall.h>
#include <unistd.h>

void* sbrk(intptr_t size) {
    return (void*) syscall1(SYS_SBRK, size);
}
