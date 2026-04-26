#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

typedef int clockid_t;
typedef long time_t;

struct timespec {
    time_t tv_sec;
    time_t tv_nsec;
};

typedef long off_t;
typedef long ssize_t;

typedef unsigned short dev_t;
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

typedef unsigned char cc_t;
typedef unsigned int tcflag_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct utsname {
    char sysname[64];
    char nodename[64];
    char release[64];
    char version[64];
    char machine[64];
};

#endif /* _KERNEL_TYPES_H */
