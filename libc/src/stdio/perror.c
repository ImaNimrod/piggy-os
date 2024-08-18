#include <errno.h>
#include <string.h>
#include "stdio_internal.h"

void perror(const char* s) {
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    }
}
