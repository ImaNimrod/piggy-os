#include <stdint.h>

#define SYS_TEST            0
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
    syscall0(SYS_TEST);

    for (;;) {
        __asm__ volatile("pause");
    }
}

int main(void) {
    syscall1(SYS_THREAD_CREATE, (uint64_t) entry);

    for (;;) {
        __asm__ volatile("pause");
    }

    return 0;
}
