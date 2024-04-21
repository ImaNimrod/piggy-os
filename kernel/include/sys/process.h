#ifndef _KERNEL_SYS_PROCESS_H
#define _KERNEL_SYS_PROCESS_H

#include <mem/vmm.h>
#include <stdint.h>
#include <sys/thread.h>
#include <types.h>
#include <utils/list.h>
#include <utils/string.h>
#include <utils/vector.h>

struct process {
    pid_t pid;
    char name[256];
    struct pagemap* pagemap;
    VECTOR_TYPE(struct thread*) threads;
    struct process* next;
};

struct process* process_create(struct pagemap* pagemap);

#endif /* _KERNEL_SYS_PROCESS_H */
