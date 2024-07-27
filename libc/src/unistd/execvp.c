#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int execvp(const char* file, char* const argv[]) {
    if (!*file) {
        return -1;
    }

    char* alloc_str = NULL;
    const char* file_path = NULL;

    if (strchr(file, '/')) {
        file_path = file;
    } else {
        const char* path_env = getenv("PATH");
        while (path_env != NULL) {
            size_t length = strcspn(path_env, ":");
            if (length == 0) {
                int fd = open(file, O_PATH);
                close(fd);
                if (fd >= 0) {
                    file_path = file;
                    break;
                }
            } else {
                alloc_str = malloc(length + strlen(file) + 2);
                if (!alloc_str) {
                    return -1;
                }

                memcpy(alloc_str, path_env, length);
                stpcpy(stpcpy(alloc_str + length, "/"), file);

                int fd = open(alloc_str, O_PATH);
                close(fd);
                if (fd >= 0) {
                    file_path = alloc_str;
                    break;
                }

                free(alloc_str);
                alloc_str = NULL;
            }
            path_env = path_env[length] ? path_env + length + 1 : NULL;
        }

        if (file_path == NULL) {
            return -1;
        }
    }

    execv(file_path, argv);

    if (errno != ENOEXEC) {
        free(alloc_str);
        return -1;
    } 

    int argc;
    for (argc = 0; argv[argc] != NULL; argc++);
    if (argc == 0) {
        argc = 1;
    }

    char** shell_argv = malloc((argc + 3) * sizeof(char*));
    if (shell_argv == NULL) {
        free(alloc_str);
        return -1;
    }

    shell_argv[0] = argv[0] ? argv[0] : (char*) file;
    shell_argv[1] = "--";
    shell_argv[2] = (char*) file_path;
    for (int i = 1; i < argc; i++) {
        shell_argv[i + 2] = argv[i];
    }
    shell_argv[argc + 2] = NULL;

    execv("/bin/sh", shell_argv);

    free(shell_argv);
    free(alloc_str);
    return -1;
}
