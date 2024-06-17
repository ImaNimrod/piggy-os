#include <stdint.h>

#define SYS_DEBUG           0
#define SYS_FORK            1
#define SYS_EXIT            2
#define SYS_YIELD           3
#define SYS_GETPID          4
#define SYS_GETTID          5
#define SYS_THREAD_CREATE   6
#define SYS_THREAD_EXIT     7

extern uint64_t syscall0(uint64_t syscall_number);
extern uint64_t syscall1(uint64_t syscall_number, uint64_t arg1);

static void entry(void) {
    const char* message = "hello, world!\n";
    syscall1(SYS_DEBUG, (uint64_t) message);

    for (;;) {
        __asm__ volatile("pause");
    }
}

int main(void) {
    for (int i = 0; i < 5; i++) {
        syscall1(SYS_THREAD_CREATE, (uint64_t) entry);
    }

    for (;;) {
        __asm__ volatile("pause");
    }

    return 0;
}
