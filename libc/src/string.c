#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void* memchr(const void* s, int c, size_t size) {
    const uint8_t* p = s;
    for (size_t i = 0; i < size; i++) {
        if (p[i] == (uint8_t) c) {
            return (void*) &p[i];
        }
    }
    return NULL;
}

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

char* stpcpy(char* __restrict str1, const char* __restrict str2) {
    while (*str2) {
        *str1++ = *str2++;
    }

    *str1 = '\0';
    return str1;
}

char* stpncpy(char* __restrict str1, const char* __restrict str2, size_t n) {
    size_t i;
    for (i = 0; i < n && str2[i] != '\0'; i++) {
        str1[i] = str2[i];
    }

    char* result = &str1[i];
    for (; i < n; i++) {
        str1[i] = '\0';
    }

    return result;
}

char* strcat(char* __restrict dest, const char* __restrict src) {
    size_t len = strlen(dest);

    for (size_t i = 0;; i++) {
        dest[len + i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }

    return dest;
}

char* strncat(char* __restrict dest, const char* __restrict src, size_t n) {
    size_t d = strlen(dest);
    size_t i = 0;

    for (i = 0; i < n && src[i]; i++) {
        dest[d + i] = src[i];
    }

    dest[d + i] = '\0';
    return dest;
}

char* strchr(const char* str, int c) {
    do {
        if (*str == (char) c) {
            return (char*) str;
        }
    } while (*str++);

    return NULL;
}

char* strrchr(const char* str, int c) {
    size_t len = strlen(str);
    if (len == 0) {
        return NULL;
    }

    while (len--) {
        if (str[len] == c) {
            return (char*) &str[len];
        }
    }

    return NULL;
}

int strcmp(const char* str1, const char* str2) {
    for (size_t i = 0;; i++) {
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
    for (; n; str1++, str2++, n--) {
        int delta = *str1 - *str2;
        if (delta) {
            return delta;
        }

        if (*str1 == '\0') {
            break;
        }
    }

    return 0;
}

int strcoll(const char* str1, const char* str2) {
    return strcmp(str1, str2);
}

char* strcpy(char* __restrict dest, const char* __restrict src) {
    char* result = dest;

    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';
    return result;
}

char* strncpy(char* __restrict dest, const char* __restrict src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];

        if (!src[i]) {
            break;
        }
    }

    return dest;
}

char* strdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* dup = malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

size_t strlen(const char* str) {
    const char* s = str;
    while (*s) {
        s++;
    }
    return (size_t) s - (size_t) str;
}

size_t strnlen(const char* str, size_t n) {
    size_t len;
    for (len = 0; len < n && str[len]; len++) {}
    return len;
}

char* strpbrk(const char* str1, const char* str2) {
    while (*str1) {
        for (size_t i = 0; str2[i]; i++) {
            if (*str1 == str2[i]) {
                return (char*) str1;
            }
        }

        str1++;
    }

    return NULL;
}

size_t strspn(const char* str, const char* accept) {
    size_t len = 0;

    while (*str != '\0') {
        if (!strchr(accept, *str)) {
            return len;
        }

        len++;
        str++;
    }

    return len;
}

size_t strcspn(const char* str, const char* characters) {
    size_t result = 0;

    while (str[result]) {
        for (size_t i = 0; characters[i]; i++) {
            if (str[result] == characters[i]) {
                return result;
            }
        }

        result++;
    }

    return result;
}

char* strstr(const char* haystack, const char* needle) {
    size_t n = strlen(needle);

    while (*haystack != '\0') {
        if (*haystack == *needle && strncmp(haystack, needle, n)) {
            return (char*) haystack;
        }
        haystack++;
    }

    return NULL;
}


static char* strtok_saveptr = NULL;

char* strtok(char* __restrict str, const char* __restrict delim) {
    if (str) {
        strtok_saveptr = NULL;
    }

    return strtok_r(str, delim, &strtok_saveptr);
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
    char* token;

    if (str == NULL) {
        str = *saveptr;
    }

    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }

    token = str;
    str = strpbrk(token, delim);

    if (str == NULL) {
        *saveptr = (char*) strchr(token, '\0');
    } else {
        *str = '\0';
        *saveptr = str + 1;
    }

    return token;
}

size_t strxfrm(char* __restrict dest, const char* __restrict src, size_t n) {
    size_t i = 0;

    while (*src && i < n) {
        *dest = *src;
        dest++;
        src++;
        i++;
    }

    if (i < n) {
        *dest = '\0';
    }
    return i;
}
