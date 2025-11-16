#ifndef _KERNEL_UTILS_VECTOR_H
#define _KERNEL_UTILS_VECTOR_H

#include <stdbool.h>
#include <stddef.h>

typedef struct vector {
    size_t capacity;
    size_t size;
    size_t item_size;
    void* data;
} vector_t;

vector_t* vector_create(size_t item_size);
void vector_destroy(vector_t* v);
void** vector_get(vector_t* v, size_t index);
bool vector_set(vector_t* v, size_t index, const void* value);
bool vector_remove(vector_t* v, size_t index);
bool vector_remove_by_value(vector_t* v, const void* value);
bool vector_push(vector_t* v, const void* value);
bool vector_pop(vector_t* v, void* out);
size_t vector_size(vector_t* v);

#endif /* _KERNEL_UTILS_VECTOR_H */
