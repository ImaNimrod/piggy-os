#ifndef _KERNEL_DEV_BLOCK_H
#define _KERNEL_DEV_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PARTITION_DEV_MAJOR 14

typedef enum {
    CMD_READ,
    CMD_WRITE,
    CMD_FLUSH,
} block_cmd_t;

struct block_device;

typedef ssize_t (*block_cmd_handler_t)(struct block_device*, block_cmd_t, uint64_t, size_t, uintptr_t);

struct block_device {
    block_cmd_handler_t cmd_handler;

    void* private;

    size_t block_count;
    size_t block_size;
    size_t lba_offset;
};

int block_register(const char* name, dev_t dev, struct block_device* block_device, bool check_partitions);
void block_init(void);

#endif /* _KERNEL_DEV_BLOCK_H */
