#include <stdint.h>

extern uint64_t syscall0(uint64_t syscall_number);

int main(void) {
    //volatile float a = 0.75f;
    //volatile float b = 0.2314f;
    //volatile float c = a / b;

    syscall0(0);
    return 0;
}
