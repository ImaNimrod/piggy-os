#include <config.h>
#include <cpu/isr.h>
#include <net/netif.h>
#include <types.h>
#include <utils/usercopy.h>

static struct utsname utsname = {
    .sysname = "Piggy",
    .release = RELEASE,
    .version = VERSION,
    .machine = "x86_64",
};

void sys_uname(struct registers* r) {
    struct utsname* buf = (struct utsname*) r->rdi;

    strncpy(utsname.nodename, hostname, sizeof(utsname.nodename));

    r->rax = user_memcpy_to_user(buf, &utsname, sizeof(utsname));
}
