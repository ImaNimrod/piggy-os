#include <locale.h>
#include <stddef.h>
#include <string.h>

char* setlocale(int category, const char* locale) {
    (void) category;

    if (!locale) {
        return "C";
    } else if (*locale == '\0') {
        return "C";
    } else if (!strcmp(locale, "C")) {
        return "C";
    } else {
        return NULL;
    }
}
