#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int clockid_t;
typedef long time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

typedef long off_t;
typedef long ssize_t;

typedef unsigned int dev_t;
typedef unsigned long ino_t;
typedef int mode_t;
typedef unsigned long nlink_t;

typedef long blksize_t;
typedef long blkcnt_t;

typedef unsigned int nfds_t;

struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

struct pollfd {
    int fd;
    short int events;
    short int revents;
};

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;

    unsigned short _st_uid; // NOT USED
    unsigned short _st_gid; // NOT USED
    unsigned int _padding;
};

typedef int pid_t;
typedef int tid_t;

typedef unsigned long sigset_t;

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int);
    };
    int sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

typedef struct {
    void* ss_sp;
    size_t ss_size;
    int ss_flags;
} stack_t;

typedef unsigned char cc_t;
typedef unsigned int tcflag_t;

#define NCCS 16

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __padding[128 - sizeof(sa_family_t) - sizeof(long)];
    long __force_alignment;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

#endif /* _KERNEL_TYPES_H */
