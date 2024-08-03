#include <stdlib.h>
#include "dirent_internal.h"

DIR* fdopendir(int fd) {
    DIR* dirp = malloc(sizeof(DIR));
    if (!dirp) {
        return NULL;
    }

    dirp->fd = fd;
    dirp->buffer_size = 0;
    dirp->offset = 0;

    return dirp;
}
