#include <unistd.h>
#include "dirent_internal.h"

struct dirent* readdir(DIR* dirp) {
    if (dirp->offset >= dirp->buffer_size) {
        ssize_t size = read(dirp->fd, (struct dirent*) dirp->buffer, BUFFER_CAPACITY);
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
