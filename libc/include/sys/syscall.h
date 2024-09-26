#ifndef _SYS_SYSCALLS_H
#define _SYS_SYSCALLS_H

#include <stdint.h>

#define SYS_EXIT            0
#define SYS_FORK            1
#define SYS_EXEC            2
#define SYS_WAIT            3
#define SYS_YIELD           4
#define SYS_GETPID          5
#define SYS_GETPPID         6
#define SYS_GETTID          7
#define SYS_THREAD_CREATE   8
#define SYS_THREAD_EXIT     9
#define SYS_SBRK            10
#define SYS_OPEN            11
#define SYS_MKDIR           12
#define SYS_CLOSE           13
#define SYS_READ            14
#define SYS_WRITE           15
#define SYS_IOCTL           16
#define SYS_SEEK            17
#define SYS_TRUNCATE        18
#define SYS_FSYNC           19
#define SYS_STAT            20
#define SYS_CHDIR           21
#define SYS_GETCWD          22
#define SYS_GETDENTS        23
#define SYS_UTSNAME         24
#define SYS_SYSACT          25
#define SYS_SLEEP           26
#define SYS_CLOCK_GETTIME   27
#define SYS_CLOCK_SETTIME   28

extern uint64_t syscall0(uint64_t);
extern uint64_t syscall1(uint64_t, uint64_t);
extern uint64_t syscall2(uint64_t, uint64_t, uint64_t);
extern uint64_t syscall3(uint64_t, uint64_t, uint64_t, uint64_t);
extern uint64_t syscall4(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#endif /* _SYS_SYSCALLS_H */
