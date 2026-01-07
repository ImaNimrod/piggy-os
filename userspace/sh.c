#include <sys/wait.h>

#include <err.h>
#include <limits.h>
#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 

#define ARRAY_SIZE(xs) (sizeof((xs)) / sizeof((xs)[0]))

struct shell_builtin {
    const char* name;
    bool (*func) (char* []);
};

static char* line;
static char** args;
static bool do_quit = false;

static void usage(void) {
    fprintf(stderr, "usage: sh [-i] [-c COMMAND]\n");
    exit(EXIT_FAILURE);
}

static bool builtin_cd(char* args[]) {
    if (chdir(args[1] != NULL ? args[1] : getenv("HOME")) < 0) {
        warn("chdir");
    }

    return false;
}

static bool builtin_exit(char* args[]) {
    (void) args;
    return true;
}

static bool builtin_pwd(char* args[]) {
    (void) args;

    char* buf = malloc(PATH_MAX);
    if (buf == NULL) {
        err(EXIT_FAILURE, "malloc");
    } else {
        if (getcwd(buf, PATH_MAX) == NULL) {
            warn("getcwd");
            return false;
        }

        puts(buf);
    }

    free(buf);
    return false;
}

static struct shell_builtin builtins[] = {
    { "cd", builtin_cd },
    { "exit", builtin_exit },
    { "pwd", builtin_pwd },
};

char* read_line(void) {
    int position = 0;

    char* buf = malloc(256 * sizeof(char));
    if (buf == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    int c;
    for (;;) {
        c = getchar();

        if (c == EOF || c == '\n') {
            buf[position] = '\0';
            return buf;
        } else {
            buf[position] = c;
        }

        position++;
    }
}

static char** split_args(char *line) {
    int position = 0;

    char** tokens = malloc(64 * sizeof(char*));
    if (tokens == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    char* token = strtok(line, " \t\n\a");
    while (token != NULL) {
        tokens[position] = token;
        position++;
        token = strtok(NULL, " \t\n\a");
    }

    tokens[position] = NULL;
    return tokens;
}

static void run_program(char** args, int* status) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return;
    }

    if (pid == 0) {
        execvp(args[0], args);
        warn("execvp");
    } else {
        if (waitpid(pid, status, 0) < 0) {
            warn("waitpid");
        }
    }
}

static bool execute(char* args[], int* status) {
    if (args[0] == NULL) {
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(builtins); i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            return (*builtins[i].func)(args);
        }
    }

    run_program(args, status);
    return false;
}

int main(int argc, char* argv[]) {
    bool run_command = false;
    char* command = NULL;
    int args_index = 0;

    int c;
    while ((c = getopt(argc, argv, "c:i")) != -1) {
        if (args_index > 0) {
            break; 
        }

        switch (c) {
            case 'c':
                run_command = true;
                command = optarg;

                if (strcmp(command, "--") == 0) {
                    args_index = optind;
                }
                break;
            case 'i':
                break;
            default:
                usage();
                break;
        }
    }

    if (run_command) {
        int status;

        if (args_index) {
            if (argc - args_index == 1) {
                char** command_args = split_args(argv[args_index]);
                execute(command_args, &status);
                free(command_args);
            } else {
                execute(&argv[args_index], &status);
            }
        } else {
            char* command_args[] = { command, NULL };
            execute(command_args, &status);
        }

        return status;
    }

    do {
        fputs("$ ", stderr);

        line = read_line();
        args = split_args(line);
        do_quit = execute(args, NULL);

        free(line);
        free(args);
    } while (!do_quit);

    return EXIT_SUCCESS;
}
