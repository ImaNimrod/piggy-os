#include <stdlib.h>
#include <unistd.h>
#include "dirent_internal.h"

int closedir(DIR* dirp) {
    int fd = dirp->fd;
    free(dirp);
    return close(fd);
}
