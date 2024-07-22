#include <sys/syscall.h>
#include <unistd.h>

char* getcwd(char* buf, size_t length) {
    return (char*) syscall2(SYS_GETCWD, (uint64_t) buf, length);
}
