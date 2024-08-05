#include <string.h>
#include <unistd.h>
#include "stdlib_internal.h"

extern char** __heap_environ;
extern size_t __heap_environ_length;

int unsetenv(const char* name) {
    if (!*name || strchr(name, '=')) {
        return -1;
    }

    if (!environ) {
        return 0;
    }

    size_t name_len = strlen(name);

    for (size_t i = 0; environ[i]; i++) {
        if (name_len == strcspn(environ[i], "=") && !strncmp(environ[i], name, name_len)) {
            if (environ == __heap_environ) {
                free(environ[i]);
                environ[i] = environ[--__heap_environ_length];
                environ[__heap_environ_length] = NULL;
            } else {
                for (size_t j = i; environ[j]; j++) {
                    environ[j] = environ[j + 1];
                }
            }

            i--;
        }
    }

    return 0;
}
