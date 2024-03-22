#include <utils/string.h>

int memcmp(const void* ptr1, const void* ptr2, size_t n) {
    const uint8_t* p1 = (const uint8_t*) ptr1;
    const uint8_t* p2 = (const uint8_t*) ptr2;

    while (n-- > 0) {
        if (*p1++ != *p2++) {
            return p1[-1] < p2[-1] ? -1 : 1;
        }
    }

    return 0;
}

void* memcpy(void* dest, const void* src, size_t n) {
    __asm__ volatile("cld; rep movsb" : "=c"((int){0}) : "D"(dest), "S"(src), "c"(n) : "flags", "memory");
    return dest;
}

void* memset(void* dest, int c, size_t n) {
    __asm__ volatile("cld; rep stosb" : "=c"((int){0}) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
    return dest;
}
