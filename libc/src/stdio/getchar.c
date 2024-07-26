#include "stdio_internal.h"

int getchar(void) {
    return fgetc(stdin);
}
