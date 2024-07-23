#include "stdio_internal.h"

// TODO: print errno error message once errno is implemented in kernel 
void perror(const char* s) {
    if (s && *s) {
        fputs(s, stderr);
    }

    fputc('\n', stderr);
}
