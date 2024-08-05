#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

typedef long int off_t;
typedef long int ssize_t;

typedef int pid_t;
typedef int tid_t;
typedef long int time_t;

typedef unsigned short int dev_t;
typedef unsigned long int ino_t;
typedef unsigned short int reclen_t;

typedef int mode_t;
typedef long int blksize_t;
typedef long int blkcnt_t;

typedef int clockid_t;

struct timespec {
    time_t tv_sec;
    long int tv_nsec;
};

typedef unsigned int tcflag_t;
typedef unsigned int cc_t;

#endif /* _SYS_TYPES_H */
