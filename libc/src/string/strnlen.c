#include <string.h>

size_t strnlen(const char* str, size_t n) {
    size_t len;
    for (len = 0; len < n && str[len]; len++);
    return len;
}
