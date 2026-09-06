#include <config.h>
#include <cpu/isr.h>
#include <mem/pmm.h>
#include <net/netif.h>
#include <types.h>
#include <utils/usercopy.h>

void sys_sysinfo(struct registers* r) {
    struct sysinfo* buf = (struct sysinfo*) r->rdi;

    struct sysinfo info = {
        .total_mem_pages = total_pages,
        .free_mem_pages = free_pages,
        .sysname = "Piggy",
        .release = RELEASE,
        .version = VERSION,
        .machine = "x86_64",
    };

    strncpy(info.hostname, hostname, sizeof(info.hostname));

    r->rax = user_memcpy_to_user(buf, &info, sizeof(info));
}
