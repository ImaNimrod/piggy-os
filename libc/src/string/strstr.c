#include <string.h>

char* strstr(const char* haystack, const char* needle) {
    while (*haystack != '\0') {
        if (*haystack == *needle && !strncmp(haystack, needle, strlen(needle))) {
            return (char*) haystack;
        }

        haystack++;
    }

    return NULL;
}
