#include <mem/slab.h>
#include <utils/string.h>
#include <utils/vector.h>

vector_t* vector_new(size_t elem_size) {
    vector_t* v = kmalloc(sizeof(vector_t));
    v->elem_size = elem_size;
    v->size = 0;
    v->capacity = VECTOR_INITIAL_CAPACITY;
    v->data = kmalloc(elem_size * VECTOR_INITIAL_CAPACITY);
    return v;
}

void vector_destroy(vector_t* v) {
    if (v != NULL) {
        if (v->data != NULL) {
            kfree(v->data);
        }
        kfree(v);
    }
}

void* vector_get(vector_t* v, size_t index) {
	if (!v || !v->data || index >= v->size) {
		return NULL;
    }

	return &v->data[v->elem_size * index];
}

bool vector_set(vector_t* v, size_t index, void* value) {
    if (!v || !v->data || index >= v->size) {
        return false;
    }

	v->data[v->elem_size * index] = value;
    return true;
}

bool vector_remove(vector_t* v, size_t index) {
    if (!v || !v->data || index >= v->size) {
        return false;
    }

    if (index == v->size) {
        vector_pop_back(v);
    } else {
        memmove(&(v->data[index]), &(v->data[index + 1]), (v->elem_size * v->size) - index - 1);
        v->size--;
    }

    return true;
}

bool vector_remove_by_value(vector_t* v, void* value) {
    if (!v || !v->data) {
        return false;
    }

    for (size_t i = 0; i < v->size; i++) {
        if (v->data[v->elem_size * i] == value) {
            vector_remove(v, i);
            return true;
        }
    }

    return false;
}

bool vector_push_back(vector_t* v, void* value) {
    if (!v || !v->data) {
        return false;
    }

    if (v->size == v->capacity) {
        v->capacity *= VECTOR_GROWTH_RATE;
        v->data = krealloc(v->data, v->capacity * v->elem_size);
    }

    v->data[v->size++] = value;
    return true;
}

void* vector_pop_back(vector_t* v) {
	if (!v || !v->data) {
		return NULL;
    }

    return &v->data[v->elem_size * v->size--];
}
