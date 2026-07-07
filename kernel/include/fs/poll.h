#ifndef _KERNEL_FS_POLL_H
#define _KERNEL_FS_POLL_H

#include <types.h>
#include <utils/vector.h>
#include <utils/wait_queue.h>

struct poll_table_entry {
    struct wait_node node;
    struct wait_queue* wq;
};

struct poll_table {
    vector_t* entries;
};

int poll_table_init(struct poll_table* pt);
void poll_table_deinit(struct poll_table* pt);

int poll_table_add(struct poll_table* pt, struct wait_queue* wq);
int poll_table_wait(struct poll_table* pt, const struct timespec* timeout);
int poll_table_reset(struct poll_table* pt);

#endif /* _KERNEL_FS_POLL_H */
