#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H 1

#include <stdint.h>

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
