#include <utils/string.h>

void* memcpy(void* dest, const void* src, size_t n) {
    __asm__ volatile("cld; rep movsb" : "=c"((int){0}) : "D"(dest), "S"(src), "c"(n) : "flags", "memory");
    return dest;
}

void* memset(void* dest, int c, size_t n) {
    __asm__ volatile("cld; rep stosb" : "=c"((int){0}) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
    return dest;
}
