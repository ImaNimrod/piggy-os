#include <cpu/isr.h>
#include <errno.h>
#include <net/netif.h>
#include <utils/usercopy.h>

void sys_sethostname(struct registers* r) {
    char* buf = (char*) r->rdi;
    size_t len = r->rsi;

    if (len >= HOST_NAME_MAX) {
        len = HOST_NAME_MAX - 1;
    }

    r->rax = user_memcpy_from_user(hostname, buf, len);
}
