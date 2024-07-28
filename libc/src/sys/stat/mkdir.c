#include <sys/stat.h>
#include <sys/syscall.h>

int mkdir(const char* path, ...) {
    return syscall1(SYS_MKDIR, (uint64_t) path);
}
