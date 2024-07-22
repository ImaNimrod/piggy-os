#include <string.h>

int memcmp(const void* ptr1, const void* ptr2, size_t n) {
    const char* p1 = (const char*) ptr1;
    const char* p2 = (const char*) ptr2;

    while (n-- > 0) {
        if (*p1++ != *p2++) {
            return p1[-1] < p2[-1] ? -1 : 1;
        }
    }

    return 0;
}
