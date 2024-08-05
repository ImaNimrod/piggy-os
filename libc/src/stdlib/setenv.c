#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "stdlib_internal.h"

static bool reset_environ(void) {
    if (__heap_environ) {
        while (*__heap_environ) {
            free(*__heap_environ++);
        }
        free(__heap_environ);
    }

    size_t length = 0;
    if (environ) {
        char** envp = environ;
        while (*envp++) {
            length++;
        }
    }

    size_t size = length > 15 ? length + 1 : 16;

    __heap_environ = malloc(size * sizeof(char*));
    if (__heap_environ == NULL) {
        return false;
    }

    __heap_environ_length = length;
    __heap_environ_size = size;

    for (size_t i = 0; i < length; i++) {
        __heap_environ[i] = strdup(environ[i]);

        if (!__heap_environ[i]) {
            for (size_t j = 0; j < i; j++) {
                free(__heap_environ[j]);
            }
            free(__heap_environ);
            __heap_environ = NULL;
            return false;
        }
    }

    __heap_environ[length] = NULL;
    environ = __heap_environ;
    return true;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!*name || strchr(name, '=')) {
        return -1;
    }

    if (!environ || environ != __heap_environ) {
        if (!reset_environ()) { 
            return -1;
        }
    }

    size_t name_len = strlen(name);

    size_t i = 0;
    for (; environ[i]; i++) {
        if (name_len == strcspn(environ[i], "=") && !strncmp(environ[i], name, name_len)) {
            if (!overwrite) {
                return 0;
            }

            size_t entry_len = name_len + strlen(value) + 2;

            char* new_entry = malloc(entry_len);
            if (!new_entry) {
                return -1;
            }

            stpcpy(stpcpy(stpcpy(new_entry, name), "="), value);

            free(environ[i]);
            environ[i] = new_entry;
            return 0;
        }
    }

    if (__heap_environ_length + 1 == __heap_environ_size) {
        char** new_environ = reallocarray(environ, __heap_environ_size, 2 * sizeof(char*));
        if (!new_environ) {
            return -1;
        }

        environ = __heap_environ = new_environ;
        __heap_environ_size *= 2;
    }

    size_t entry_len = name_len + strlen(value) + 2;
    environ[i] = malloc(entry_len);
    if (!environ[i]) {
        return -1;
    }

    stpcpy(stpcpy(stpcpy(environ[i], name), "="), value);

    environ[i + 1] = NULL;
    __heap_environ_length++;
    return 0;
}
