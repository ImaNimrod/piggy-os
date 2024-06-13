#ifndef _KERNEL_UTILS_VECTOR_H
#define _KERNEL_UTILS_VECTOR_H

#include <stddef.h>

#define VECTOR_GROWTH_RATE 2 
#define VECTOR_INITIAL_CAPACITY 4
#define VECTOR_INVALID_INDEX (-1)

typedef struct {
    size_t elem_size;
    size_t size;
    size_t capacity;
    void** data;
} vector_t;

vector_t* vector_new(size_t elem_size);
void vector_destroy(vector_t* v);
void* vector_get(vector_t* v, size_t index);
bool vector_set(vector_t* v, size_t index, void* value);
bool vector_remove(vector_t* v, size_t index);
bool vector_remove_by_value(vector_t* v, void* value);
bool vector_push_back(vector_t* v, void* value);
void* vector_pop_back(vector_t* v);

#endif /* _KERNEL_UTILS_VECTOR_H */
