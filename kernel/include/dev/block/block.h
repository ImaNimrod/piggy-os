#ifndef _KERNEL_DEV_BLOCK_H
#define _KERNEL_DEV_BLOCK_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

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
};

int block_register(const char* name, dev_t dev, block_cmd_handler_t cmd_handler, void* private, size_t block_count, size_t block_size);
void block_init(void);

#endif /* _KERNEL_DEV_BLOCK_H */
