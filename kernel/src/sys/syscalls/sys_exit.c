#include <cpu/isr.h>
#include <sys/process.h>

void sys_exit(struct registers* r) {
    int status = r->rdi;

    process_exit(status);
}
