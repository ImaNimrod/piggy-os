#include <sys/sysact.h>
#include <sys/syscall.h>

void sysact(unsigned long int magic1, unsigned long int magic2, unsigned long int magic3, int action) {
    syscall4(SYS_SYSACT, magic1, magic2, magic3, action);
}
