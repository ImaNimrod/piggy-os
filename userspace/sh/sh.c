#include <sys/wait.h>

#include <err.h>
#include <limits.h>
#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 

#include "builtins.h"
#include "history.h"
#include "sh.h"

#define LINE_LENGTH 256

char cwd[PATH_MAX];
int last_status;

static char line_buf[LINE_LENGTH];

static void read_line(char* buf) {
    int position = 0;

    int c;
    for (;;) {
        c = getchar();

        if (c == EOF || c == '\n') {
            buf[position] = '\0';
            return;
        } else {
            buf[position] = c;
        }

        position++;
    }
}

static int run_program(char** argv) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        err(EXIT_FAILURE, "execvp");
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        warn("waitpid");
    }

    return WEXITSTATUS(status);
}

static void usage(void) {
    fprintf(stderr, "usage: sh [-i] [-c COMMAND]\n");
    exit(EXIT_FAILURE);
}

int execute(int argc, char* argv[]) {
    if (argc == 0) {
        return EXIT_SUCCESS;
    }

    size_t i = 0;
    while (BUILTINS[i].name != NULL) {
        if (strcmp(argv[0], BUILTINS[i].name) == 0) {
            return (*BUILTINS[i].func)(argc, argv);
        }

        i++;
    }

    return run_program(argv);
}

int split_args(char* line, char*** argv) {
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

int main(int argc, char* argv[]) {
    bool run_command = false;

    char* command = NULL;
    int args_index = 0;

    setpwd();

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
        }
    }

    if (run_command) {
        int ret = EXIT_SUCCESS;

        if (args_index != 0) {
            if (argc - args_index == 1) {
                char** command_argv;
                int command_argc = split_args(argv[args_index], &command_argv);

                ret = execute(command_argc, command_argv);

                free(command_argv);
            } else {
                ret = execute(argc - args_index, &argv[args_index]);
            }
        } else {
            ret = execute(1, (char* []) { command, NULL });
        }

        return ret;
    }

    if (argc > 1) {
        FILE* fp = fopen(argv[1], "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, argv[1]);
        }

        char* line = NULL;
        size_t len = 0;

        int ret = EXIT_SUCCESS;

        while (getline(&line, &len, fp) != -1) {
            char** argv;
            int argc = split_args(line, &argv);

            ret = execute(argc, argv);

            free(argv);
        }

        free(line);
        fclose(fp);
        return ret;
    }

    for (;;) {
        fprintf(stderr, "\033[94msh\033[0m:\033[32m%s\033[0m> ", cwd);

        read_line(line_buf);
        if (line_buf[0] != '\0') {
            history_push(line_buf);
        }

        char** argv;
        int argc = split_args(line_buf, &argv);

        last_status = execute(argc, argv);

        free(argv);
    }
}
