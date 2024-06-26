#include <stddef.h>
#include <stdint.h>

#define SYS_DEBUG           0
#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_EXEC            3
#define SYS_YIELD           4
#define SYS_GETPID          5
#define SYS_GETTID          6
#define SYS_THREAD_CREATE   7
#define SYS_THREAD_EXIT     8
#define SYS_OPEN            9
#define SYS_CLOSE           10
#define SYS_READ            11
#define SYS_WRITE           12
#define SYS_IOCTL           13
#define SYS_SEEK            14
#define SYS_CHDIR           15
#define SYS_GETCWD          16

extern uint64_t syscall0(uint64_t syscall_number);
extern uint64_t syscall1(uint64_t syscall_number, uint64_t arg1);
extern uint64_t syscall2(uint64_t syscall_number, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3);


int main(void) {
    syscall1(SYS_DEBUG, (uint64_t) "hello, world\n");
    return 0;
}
