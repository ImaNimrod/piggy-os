#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: finish implementing this with $PATH
int execvp(const char* file, char* const argv[]) {
    if (!*file) {
        return -1;
    }

    const char* file_path;

    if (strchr(file, '/')) {
        file_path = file;
    } else {
        return -1;
    }

    execv(file_path, argv);

    int argc;
    for (argc = 0; argv[argc] != NULL; argc++);
    if (argc == 0) {
        argc = 1;
    }

    char** shell_argv = malloc((argc + 3) * sizeof(char*));
    if (shell_argv == NULL) {
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
    return -1;
}
