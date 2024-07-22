#include <sys/syscall.h>
#include <sys/utsname.h>

int utsname(struct utsname* utsname) {
    return syscall1(SYS_UTSNAME, (uint64_t) utsname);
}
