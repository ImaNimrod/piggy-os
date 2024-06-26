#include <mem/slab.h>
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

int strcmp(const char* str1, const char* str2) {
    for (size_t i = 0; ; i++) {
        char c1 = str1[i];
        char c2 = str2[i];

        if (c1 != c2) {
            return c1 - c2;
        }

        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}

int strncmp(const char* str1, const char* str2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = str1[i];
        char c2 = str2[i];

        if (c1 != c2) {
            return c1 - c2;
        }

        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;

    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }

    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

char* strdup(const char* str) {
    size_t len = strlen(str);
    char* dup = kmalloc(len + 1);
    if (!dup) {
        return NULL;
    }
    memcpy(dup, str, len + 1);
    return dup;
}

size_t strlen(const char* str) {
    const char* s = str;
    while (*s) {
        s++;
    }
    return (size_t) s - (size_t) str;
}

char* basename(char* str) {
    if (str == NULL || !*str) {
        return ".";
    }

    size_t i = strlen(str) - 1;
    for (; i && str[i] == '/'; i--) {
        str[i] = '\0';
    }

    for (; i && str[i - 1] != '/'; i--);
    return str + i;
}
