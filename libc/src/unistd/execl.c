#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

int execl(const char* path, const char* argv0, ...) {
    int argc = 1;

    va_list args;
    va_start(args, argv0);
    while (va_arg(args, char*)) {
        argc++;
    }
    va_end(args);

    char** argv = malloc((argc + 1) * sizeof(char*));
    if (argv == NULL) {
        return -1;
    }

    va_start(args, argv0);
    argv[0] = (char*) argv0;
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(args, char*);
    }
    argv[argc] = NULL;
    va_end(args);

    execv(path, argv);

    free(argv);
    return -1;
}
