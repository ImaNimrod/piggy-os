#include <mem/slab.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>
#include <utils/vector.h>

#define VECTOR_GROWTH_FACTOR 2
#define VECTOR_INITIAL_CAPACITY 4

vector_t* vector_create(size_t item_size) {
    vector_t* v = kmalloc(sizeof(vector_t));
    if (unlikely(v == NULL)) {
        return NULL;
    }

    v->data = kmalloc(VECTOR_INITIAL_CAPACITY * item_size);
    if (unlikely(v->data == NULL)) {
        kfree(v);
        return NULL;
    }

    v->capacity = VECTOR_INITIAL_CAPACITY;
    v->size = 0;
    v->item_size = item_size;

    return v;
}

void vector_destroy(vector_t* v) {
    kfree(v->data);
    kfree(v);
}

void** vector_get(vector_t* v, size_t index) {
    if (index >= v->size) {
        return NULL;
    }

    return (void**) ((uintptr_t) v->data + (index * v->item_size));
}

bool vector_set(vector_t* v, size_t index, const void* value) {
    if (index >= v->size) {
        return false;
    }

    uintptr_t dest = (uintptr_t) v->data + (index * v->item_size);
    memcpy((void*) dest, value, v->item_size);
    return true;
}

bool vector_remove(vector_t* v, size_t index) {
    if (index >= v->size) {
        return false;
    }

    if (index == v->size - 1) {
        return vector_pop(v, NULL);
    }

    v->size--;

    void* src = (void*) ((uintptr_t) v->data + ((index + 1) * v->item_size));
    void* dest = (void*) ((uintptr_t) v->data + (index * v->item_size));
    memmove(dest, src, (v->size - index) * v->item_size);

    return true;
}

bool vector_remove_by_value(vector_t* v, const void* value) {
    for (size_t i = 0; i < v->size; i++) {
        if (!memcmp((void*) ((uintptr_t) v->data + (i * v->item_size)), value, v->item_size)) {
            vector_remove(v, i);
            return true;
        }
    }

    return false;
}

bool vector_push(vector_t* v, const void* value) {
    if (v->size == v->capacity) {
        v->capacity *= VECTOR_GROWTH_FACTOR;
        v->data = krealloc(v->data, v->capacity * v->item_size);
        if (v->data == NULL) {
            kpanic(NULL, false, "failed to grow vector");
        }
    }

    uintptr_t dest = (uintptr_t) v->data + (v->size * v->item_size);
    memcpy((void*) dest, value, v->item_size);
    v->size++;
    return true;
}

bool vector_pop(vector_t* v, void* out) {
    if (v->size == 0) {
        return false;
    }

    v->size--;

    if (likely(out != NULL)) {
        uintptr_t src = (uintptr_t) v->data + (v->size * v->item_size);
        memcpy(out, (const void*) src, v->item_size);
    }

    return true;
}

bool vector_resize(vector_t* v, size_t new_size) {
    if (new_size <= v->size) {
        v->size = new_size;
        return true;
    }

    if (new_size > v->capacity) {
        size_t new_capacity = v->capacity;
        while (new_capacity < new_size) {
            new_capacity *= VECTOR_GROWTH_FACTOR;
        }

        void* new_data = krealloc(v->data, new_capacity * v->item_size);
        if (unlikely(new_data == NULL)) {
            return false;
        }

        v->data = new_data;
        v->capacity = new_capacity;
    }

    memset((uint8_t*) v->data + (v->size * v->item_size), 0, (new_size - v->size) * v->item_size);

    v->size = new_size;
    return true;
}

size_t vector_size(vector_t* v) {
    return v->size;
}
