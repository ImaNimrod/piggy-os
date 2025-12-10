#include <cpu/asm.h>
#include <cpu/isr.h>
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
        case ARCHCTL_GET_FS_BASE: {
            uint64_t fs = rdmsr(IA32_FS_BASE_MSR);
            ret = user_memcpy_to_user(arg, &fs, sizeof(uint64_t));
            break;
        }
        case ARCHCTL_GET_GS_BASE: {
            uint64_t gs = rdmsr(IA32_KERNEL_GS_BASE_MSR);
            ret = user_memcpy_to_user(arg, &gs, sizeof(uint64_t));
            break;
        }
        case ARCHCTL_SET_FS_BASE:
            wrmsr(IA32_FS_BASE_MSR, (uint64_t) arg);
            break;
        case ARCHCTL_SET_GS_BASE:
            wrmsr(IA32_KERNEL_GS_BASE_MSR, (uint64_t) arg);
            break;
        default: 
            r->rax = -EINVAL;
            return;
    }

    r->rax = ret;
}
