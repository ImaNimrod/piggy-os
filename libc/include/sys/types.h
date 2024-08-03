#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

typedef long off_t;
typedef long ssize_t;

typedef int pid_t;
typedef int tid_t;
typedef long time_t;

typedef unsigned short dev_t;
typedef unsigned long ino_t;
typedef unsigned short reclen_t;

typedef int mode_t;
typedef long blksize_t;
typedef long blkcnt_t;

typedef int clockid_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#endif /* _SYS_TYPES_H */
