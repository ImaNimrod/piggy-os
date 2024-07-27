#include "stdio_internal.h"

// TODO: print errno error message once errno is implemented in kernel 
void perror(const char* s) {
    if (s && *s) {
        fprintf(stderr, "%s\n", s);
    }
}
