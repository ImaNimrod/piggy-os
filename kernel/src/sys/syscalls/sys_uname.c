#include <cpu/isr.h>
#include <types.h>
#include <utils/usercopy.h>

// TODO: update nodename (hostname) in the future
static struct utsname kernel_utsname = {
    .sysname = "Piggy",
    .nodename = "piggy",
    .release = "",
    .version = "",
    .machine = "x86_64",
};

void sys_uname(struct registers* r) {
    struct utsname* buf = (struct utsname*) r->rdi;
    r->rax = user_memcpy_to_user(buf, &kernel_utsname, sizeof(struct utsname));
}
