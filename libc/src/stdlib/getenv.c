#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
