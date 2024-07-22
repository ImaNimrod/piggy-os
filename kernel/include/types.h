#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define MAX_FDS 16
#define PATH_MAX 4096

#define O_PATH      0200
#define O_RDONLY    0000
#define O_WRONLY    0001
#define O_RDWR      0002

#define O_ACCMODE   (03 | O_PATH)

#define O_CREAT     0004
#define O_DIRECTORY 0010
#define O_TRUNC     0020
#define O_APPEND    0040
#define O_EXCL      0100
#define O_CLOEXEC   0200

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_IFMT      0x0f000
#define S_IFBLK     0x01000
#define S_IFCHR     0x08000
#define S_IFREG     0x03000
#define S_IFDIR     0x05000

#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#define WNOHANG     (1 << 0)
#define WUNTRACED   (1 << 1)

#define makedev(maj, min) (dev_t) ((((maj) << 8) & 0xff00u) | ((min) & 0x00ffu))
#define major(dev) (uint8_t) (((dev) & 0xff00u) >> 8)
#define minor(dev) (uint8_t) ((dev) & 0x00ffu)

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef int32_t pid_t;
typedef int32_t tid_t;

typedef uint32_t dev_t;
typedef uint64_t ino_t;

typedef int32_t mode_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
};

struct utsname {
    char sysname[64];
    char nodename[100];
    char release[64];
    char version[64];
    char machine[64];
};

#endif /* _KERNEL_TYPES_H */
