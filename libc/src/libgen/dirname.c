#include <libgen.h>
#include <string.h>

char* dirname(char* path) {
    if (!path || *path == '\0') {
        return ".";
    }

    size_t i = strlen(path) - 1;

    for (; path[i] == '/'; i--) {
        if (!i) {
            return "/";
        }
    }
    for (; path[i] != '/'; i--) {
        if (!i) {
            return ".";
        }
    }
    for (; path[i]=='/'; i--) {
        if (!i) {
            return "/";
        }
    }

    path[i + 1] = '\0';
    return path;
}
