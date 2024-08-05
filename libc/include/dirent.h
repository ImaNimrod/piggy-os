#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/types.h>

#define DT_UNKNOWN  0
#define DT_REG      1
#define DT_DIR      2
#define DT_CHR      3
#define DT_BLK      4

struct dirent {
    ino_t d_ino;
    reclen_t d_reclen;
    unsigned char d_type;
    char d_name[];
};

typedef struct __DIR DIR;

int closedir(DIR*);
int dirfd(DIR*);
DIR* fdopendir(int);
DIR* opendir(const char*);
struct dirent* readdir(DIR*);

#endif /* _DIRENT_H */
