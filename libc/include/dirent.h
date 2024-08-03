#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/types.h>

struct dirent {
    ino_t d_ino;
    reclen_t d_reclen;
    char d_name[];
};

typedef struct __DIR DIR;

int closedir(DIR*);
int dirfd(DIR*);
DIR* fdopendir(int);
DIR* opendir(const char*);
struct dirent* readdir(DIR*);

#endif /* _DIRENT_H */
