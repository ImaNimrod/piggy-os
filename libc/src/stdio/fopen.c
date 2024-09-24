#include <errno.h>
#include <unistd.h>
#include "stdio_internal.h"

FILE* fopen(const char* path, const char* mode) {
    int flags = __fopen_mode_to_flags(mode);
    if (flags == -1) {
        errno = EINVAL;
        return NULL;
    }

    if (!(flags & O_CREAT)) {
        flags &= ~O_EXCL;
    }

    int fd = open(path, flags);
    if (fd < 0) {
        return NULL;
    }

    FILE* stream = fdopen(fd, mode);
    if (stream == NULL) {
        close(fd);
    }

    return stream;
}
