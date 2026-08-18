#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <utils/usercopy.h>

#define ARCHCTL_GET_FS_BASE 0
#define ARCHCTL_GET_GS_BASE 1
#define ARCHCTL_SET_FS_BASE 2
#define ARCHCTL_SET_GS_BASE 3

void sys_archctl(struct registers* r) {
    int op = r->rdi;
    void* arg = (void*) r->rsi;

    if (!IS_USER_ADDRESS(arg)) {
        r->rax = -EFAULT;
        return;
    }

    int ret = 0;

    switch (op) {
        case ARCHCTL_GET_FS_BASE:
            uint64_t fs = this_cpu()->read_fs_base();
            ret = user_memcpy_to_user(arg, &fs, sizeof(fs));
            break;
        case ARCHCTL_GET_GS_BASE:
            uint64_t gs = rdmsr(MSR_IA32_KERNEL_GS_BASE);
            ret = user_memcpy_to_user(arg, &gs, sizeof(gs));
            break;
        case ARCHCTL_SET_FS_BASE:
            this_cpu()->write_fs_base((uint64_t) arg);
            break;
        case ARCHCTL_SET_GS_BASE:
            wrmsr(MSR_IA32_KERNEL_GS_BASE, (uint64_t) arg);
            break;
        default: 
            ret = -EINVAL;
            break;
    }

    r->rax = ret;
}
