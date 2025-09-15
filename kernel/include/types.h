#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H 1

#include <stdint.h>

#define makedev(maj, min) (dev_t) ((((maj) << 8) & 0xff00u) | ((min) & 0x00ffu))
#define major(dev) (uint8_t) (((dev) & 0xff00u) >> 8)
#define minor(dev) (uint8_t) ((dev) & 0x00ffu)

typedef int32_t pid_t;
typedef int32_t tid_t;

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef uint16_t dev_t;
typedef uint64_t ino_t;
typedef int32_t mode_t;

typedef int64_t blksize_t;
typedef int64_t blkcnt_t;

typedef int64_t time_t;

struct timespec {
    time_t tv_sec;
    time_t tv_nsec;
};

#define O_PATH      00200
#define O_RDONLY    00000
#define O_WRONLY    00001
#define O_RDWR      00002

#define O_ACCMODE   (03 | O_PATH)

#define O_CREAT     00004
#define O_DIRECTORY 00010
#define O_TRUNC     00020
#define O_APPEND    00040
#define O_EXCL      00100
#define O_NONBLOCK  00400
#define O_CLOEXEC   01000

#define S_IFMT      0x0f000
#define S_IFBLK     0x01000
#define S_IFCHR     0x08000
#define S_IFREG     0x03000
#define S_IFDIR     0x05000

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

typedef uint8_t mac_address_t[6];
typedef uint32_t ipv4_address_t;

#endif /* _KERNEL_TYPES_H */
