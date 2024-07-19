#ifndef _SYS_SYSCALLS_H
#define _SYS_SYSCALLS_H

#include <stdint.h>

#define SYS_DEBUG           0
#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_EXEC            3
#define SYS_WAIT            4
#define SYS_YIELD           5
#define SYS_GETPID          6
#define SYS_GETPPID         7
#define SYS_GETTID          8
#define SYS_THREAD_CREATE   9
#define SYS_THREAD_EXIT     10
#define SYS_SBRK            11
#define SYS_OPEN            12
#define SYS_CLOSE           13
#define SYS_READ            14
#define SYS_WRITE           15
#define SYS_IOCTL           16
#define SYS_SEEK            17
#define SYS_TRUNCATE        18
#define SYS_STAT            19
#define SYS_CHDIR           20
#define SYS_GETCWD          21

extern uint64_t syscall0(uint64_t);
extern uint64_t syscall1(uint64_t, uint64_t);
extern uint64_t syscall2(uint64_t, uint64_t, uint64_t);
extern uint64_t syscall3(uint64_t, uint64_t, uint64_t, uint64_t);

#endif /* _SYS_SYSCALLS_H */
