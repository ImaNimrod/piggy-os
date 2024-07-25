#ifndef _KERNEL_UTILS_RINGBUF_H
#define _KERNEL_UTILS_RINGBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <utils/spinlock.h>

typedef struct {
    size_t elem_size;
    size_t size;
    size_t capacity;
    size_t read_ptr;
    size_t write_ptr;
    void* data;
    spinlock_t lock;
} ringbuf_t;

ringbuf_t* ringbuf_create(size_t size, size_t elem_size);
void ringbuf_destroy(ringbuf_t* r);
bool ringbuf_push(ringbuf_t* r, const void* data);
bool ringbuf_pop(ringbuf_t* r, void* data);
bool ringbuf_pop_tail(ringbuf_t* r, void* data);
bool ringbuf_peek(ringbuf_t* r, void* data);

#endif /* _KERNEL_UTILS_RINGBUF_H */
