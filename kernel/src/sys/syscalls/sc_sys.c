#include <config.h>
#include <cpu/isr.h>
#include <types.h>
#include <utils/user_access.h>

static struct utsname system_utsname = {
    .sysname = CONFIG_SYSNAME,
    .nodename = "piggy",
    .release = CONFIG_RELEASE,
    .version = CONFIG_VERSION,
    .machine = "x86_64"
};

void syscall_utsname(struct registers* r) {
    struct utsname* utsname = (struct utsname*) r->rdi;

    if (!check_user_ptr(utsname)) {
        r->rax = -1;
        return;
    }

    copy_to_user(utsname, &system_utsname, sizeof(struct utsname));
    r->rax = 0;
}
