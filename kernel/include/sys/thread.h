#ifndef _KERNEL_SYS_THREAD_H
#define _KERNEL_SYS_THREAD_H

#include <cpu/isr.h>
#include <stdint.h>
#include <types.h>

struct thread {
    struct thread* self;
    uint64_t errno;
    tid_t id;
    struct registers r;
    uint64_t cr3;
    void* fpu_storage;
	uint64_t gs_base;
	uint64_t fs_base;
    void* kernel_stack;
};

#endif /* _KERNEL_SYS_THREAD_H */
