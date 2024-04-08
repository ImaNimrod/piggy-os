#ifndef _KERNEL_MEM_HEAP_H
#define _KERNEL_MEM_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_VADDR 0xffffff8000400000ULL
#define HEAP_SIZE 0x800000

void* kmalloc(size_t size);
void* kmalloc_align(size_t size, size_t alignment);
void kfree(void* ptr);
void* kcalloc(size_t nmemb, size_t size);
void* krealloc(void* ptr, size_t size);
void heap_init(void);

#endif /* _KERNEL_MEM_HEAP_H */
