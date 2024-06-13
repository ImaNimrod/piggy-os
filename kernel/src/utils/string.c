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

void* memmove(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;

    if (d == s) {
        return d;
    }

    if (s + n <= d || d + n <= s) {
        return memcpy(d, s, n);
    }

    if (d < s) {
        if ((uintptr_t) s % sizeof(size_t) == (uintptr_t) d % sizeof(size_t)) {
            while ((uintptr_t) d % sizeof(size_t)) {
                if (!n--) {
                    return dest;
                }
                *d++ = *s++;
            }

            for (; n >= sizeof(size_t); n -= sizeof(size_t), d += sizeof(size_t), s += sizeof(size_t)) {
                *(size_t*) d = *(size_t*) s;
            }
        }

        for (; n; n--) {
            *d++ = *s++;
        }
    } else {
        if ((uintptr_t) s % sizeof(size_t) == (uintptr_t) d % sizeof(size_t)) {
            while ((uintptr_t) (d + n) % sizeof(size_t)) {
                if (!n--) {
                    return dest;
                }
                d[n] = s[n];
            }

            while (n >= sizeof(size_t)) {
                n -= sizeof(size_t);
                *(size_t*) (d + n) = *(size_t*) (s + n);
            }
        }

        while (n) {
            n--;
            d[n] = s[n];
        }
    }

    return dest;
}

void* memset(void* dest, int c, size_t n) {
    __asm__ volatile("cld; rep stosb" : "=c"((int){0}) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
    return dest;
}
