#include <cpu/isr.h>
#include <utils/log.h>

void syscall_handler(struct registers* r) {
    klog("syscall %zu\n", r->rax);
}
