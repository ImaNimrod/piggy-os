#include "stdlib_internal.h"

void qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
    if (num < 2) {
        return;
    }

    char* array = (char*) base;
    char* pivot = array + (num - 1) * size;
    char* i = array;

    for (size_t j = 0; j < num - 1; j++) {
        if (compar(array + j * size, pivot) < 0) {
            for (size_t k = 0; k < size; k++) {
                char temp = array[(i - array) * size + k];
                array[(i - array) * size + k] = array[j * size + k];
                array[j * size + k] = temp;
            }
            i += size;
        }
    }

    for (size_t k = 0; k < size; k++) {
        char temp = array[(i - array) * size + k];
        array[(i - array) * size + k] = *pivot;
        *pivot = temp;
    }

    qsort(array, (i - array) / size, size, compar);
    qsort(i + size, num - ((i - array) / size + 1), size, compar);
}
