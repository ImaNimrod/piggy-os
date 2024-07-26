#include <string.h>

char* strtok_r(char* str, const char* seperators, char** saveptr) {
    char* token;

    if (str == NULL) {
        str = *saveptr;
    }

    str += strspn(str, seperators);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }

    token = str;
    str = strpbrk(token, seperators);

    if (str == NULL) {
        *saveptr = (char*) strchr(token, '\0');
    } else {
        *str = '\0';
        *saveptr = str + 1;
    }

    return token;
}
