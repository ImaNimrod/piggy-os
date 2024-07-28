#include <libgen.h>
#include <string.h>

char* dirname(char* path) {
    static const char* cwd = ".";
    if (!path || *path == '\0') {
        return (char*) cwd;
    }

    size_t size = strlen(path);
    while (size > 0 && path[size - 1] == '/') {
        path[--size] = '\0';
    }

    if (*path == '\0') {
        *path = '/';
        return path;
    }

    char* last_sep = strrchr(path, '/');
    if (!last_sep) {
        return (char*) cwd;
    }

    if (last_sep == path) {
        last_sep++;
    }

    do {
        *last_sep-- = '\0';
    } while (*last_sep == '/' && last_sep != path);

    return path;
}
