#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 

#define ARRAY_SIZE(xs) (sizeof((xs)) / sizeof((xs)[0]))

// TODO: make builtin commands set an int* status on completion 
struct shell_builtin {
    const char* name;
    bool (*func) (int, char* []);
};

extern char** environ;

static bool builtin_cd(int argc, char* argv[]);
static bool builtin_exec(int argc, char* argv[]);
static bool builtin_exit(int argc, char* argv[]);
static bool builtin_export(int argc, char* argv[]);
static bool builtin_pwd(int argc, char* argv[]);
static bool builtin_source(int argc, char* argv[]);
static bool builtin_unset(int argc, char* argv[]);

static bool execute(int argc, char* argv[], int* status);
static void run_program(char** argv, int* status);
static int split_args(char* line, char*** argv);

static struct shell_builtin builtins[] = {
    { "cd", builtin_cd },
    { "exec", builtin_exec },
    { "exit", builtin_exit },
    { "export", builtin_export },
    { "pwd", builtin_pwd },
    { "source", builtin_source },
    { "unset", builtin_unset },
};

static inline bool is_valid_variable(char* s) {
    if (s == NULL || (!isalpha(*s) && *s != '_')) {
        return false;
    }

    for (size_t i = 1; s[i] && s[i] != '='; i++) {
        if (!isalnum(s[i]) && s[i] != '_') {
            return false;
        }
    }

    return true;
}

static bool builtin_cd(int argc, char* argv[]) {
    (void) argc;

    if (chdir(argv[1] != NULL ? argv[1] : getenv("HOME")) < 0) {
        warn(argv[1]);
    }

    return false;
}

static bool builtin_exec(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    if (argc == 1) {
        return false;
    }

    execvp(argv[1], &argv[1]);

    warn(argv[1]);
    return false;
}

static bool builtin_exit(int argc, char* argv[]) {
    (void) argc;
    (void) argv;
    return true;
}

static bool builtin_export(int argc, char* argv[]) {
    if (argc == 1) {
        for (char** env = environ; *env != NULL; env++) {
            puts(*env);
        }

        return false;
    }

    for (int i = 1; i < argc; i++) {
        char* eq = strchr(argv[i], '=');

        if (!is_valid_variable(argv[i])) {
            fprintf(stderr, "export: invalid pattern `%s`\n", argv[i]);
            continue;
        }

        if (eq != NULL) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        } else {
            if (!getenv(argv[i])) {
                setenv(argv[i], "", 0);
            }
        }
    }

    return false;
}

static bool builtin_pwd(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

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

static bool builtin_source(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "source: filename required\n");
        return false;
    }

    FILE* fp = fopen(argv[1], "r");
    if (fp == NULL) {
        warn(argv[1]);
        return false;
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    while ((nread = getline(&line, &len, fp)) != -1) {
        char** argv;
        int argc = split_args(line, &argv);
        execute(argc, argv, NULL);
        free(argv);
    }

    free(line);
    fclose(fp);
    return false;
}

static bool builtin_unset(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (unsetenv(argv[i]) < 0) {
            warn("unsetenv(%s)", argv[i]);
        }
    }

    return false;
}

static bool execute(int argc, char* argv[], int* status) {
    if (argc == 0) {
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(builtins); i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            return (*builtins[i].func)(argc, argv);
        }
    }

    run_program(argv, status);
    return false;
}

static char* read_line(void) {
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

static void run_program(char** argv, int* status) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        warn("execvp");
    } else {
        if (waitpid(pid, status, 0) < 0) {
            warn("waitpid");
        }
    }
}

static int split_args(char* line, char*** argv) {
    int count = 0;

    char** tokens = malloc(64 * sizeof(char*));
    if (tokens == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    char* token = strtok(line, " \t\n\a");
    while (token != NULL) {
        tokens[count] = token;
        count++;
        token = strtok(NULL, " \t\n\a");
    }

    tokens[count] = NULL;

    *argv = tokens;
    return count;
}

static void usage(void) {
    fprintf(stderr, "usage: sh [-i] [-c COMMAND]\n");
    exit(EXIT_FAILURE);
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

        if (args_index != 0) {
            if (argc - args_index == 1) {
                char** command_argv;
                int command_argc = split_args(argv[args_index], &command_argv);
                execute(command_argc, command_argv, &status);
                free(command_argv);
            } else {
                execute(argc - args_index, &argv[args_index], &status);
            }
        } else {
            execute(1, (char* []) { command, NULL }, &status);
        }

        return status;
    }

    if (argc > 1) {
        FILE* fp = fopen(argv[1], "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, argv[1]);
        }

        char* line = NULL;
        size_t len = 0;
        int status = 0;

        while (getline(&line, &len, fp) != -1) {
            char** argv;
            int argc = split_args(line, &argv);
            execute(argc, argv, &status);
            free(argv);
        }

        free(line);
        fclose(fp);
        return status;
    }

    bool do_quit = false;
    do {
        fputs("$ ", stderr);

        char* line = read_line();

        char** argv;
        int argc = split_args(line, &argv);

        do_quit = execute(argc, argv, NULL);

        free(line);
        free(argv);
    } while (!do_quit);

    return EXIT_SUCCESS;
}
