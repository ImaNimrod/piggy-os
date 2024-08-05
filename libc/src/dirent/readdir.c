#include <stdlib.h>
#include <sys/syscall.h>
#include "dirent_internal.h"

static inline ssize_t getdents(int fd, struct dirent* buf, size_t count) {
    return syscall3(SYS_GETDENTS, fd, (uint64_t) buf, count);
}

struct dirent* readdir(DIR* dirp) {
    if (dirp->offset >= dirp->buffer_size) {
        ssize_t size = getdents(dirp->fd, (struct dirent*) dirp->buffer, BUFFER_CAPACITY);
        if (size <= 0) {
            dirp->buffer_size = 0;
            return NULL;
        }

        dirp->buffer_size = size;
        dirp->offset = 0;
    }

    struct dirent* ent = (struct dirent*) ((uintptr_t) dirp->buffer + dirp->offset);
    dirp->offset += ent->d_reclen;
    return ent;
}
