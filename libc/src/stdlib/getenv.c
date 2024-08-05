#include <string.h>
#include <unistd.h>
#include "stdlib_internal.h"

char* getenv(const char* name) {
    size_t name_len = strlen(name);

    char** envp = environ;
    while (*envp) {
        size_t length = strcspn(*envp, "=");
        if (length == name_len && !strncmp(name, *envp, length)) {
            return *envp + length + 1;
        }

        envp++;
    }

    return NULL;
}
