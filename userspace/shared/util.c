#include <stdio.h>

#include "util.h"

bool get_prompt(void) {
    char c, first;
    first = c = getchar();

    while (c != '\n' && c != EOF) {
        c = getchar();
    }

    return first == 'Y' || first == 'y';
}
