#include "dirent_internal.h"

int dirfd(DIR* dirp) {
    return dirp->fd;
}
