#include <string.h>


char* strtok(char* str, const char* seperators) {
    static char* next = NULL;
    if (str) {
        next = NULL;
    }

    return strtok_r(str, seperators, &next);
}
