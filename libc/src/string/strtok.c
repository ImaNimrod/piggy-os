#include <string.h>

static char* strtok_saveptr = NULL;

char* strtok(char* __restrict str, const char* __restrict delim) {
    if (str) {
        strtok_saveptr = NULL;
    }

    return strtok_r(str, delim, &strtok_saveptr);
}
