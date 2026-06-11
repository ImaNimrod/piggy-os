#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <signal.h> 
#include <string.h> 
#include <unistd.h> 

#include "builtins.h"
#include "history.h"
#include "sh.h"

char cwd[PATH_MAX];
int last_status;

static FILE* input;
static bool is_interactive;
static pid_t shell_pgid;

static int run_program(char** argv) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        setpgid(0, 0);

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        execvp(argv[0], argv);
        err(EXIT_FAILURE, "execvp");
    }

    setpgid(pid, pid);

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }

        warn("waitpid");
        break;
    }

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        if (is_interactive) {
            if (signal == SIGINT) {
                fputc('\n', stdout);
            } else {
                fprintf(stderr, "%s\n", strsignal(signal));
            }
        }

        return 128 + signal;
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

    input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            err(EXIT_FAILURE, argv[1]);
        }
    }

    is_interactive = isatty(STDIN_FILENO);

    if (is_interactive) {
        shell_pgid = getpid();
        setpgid(shell_pgid, shell_pgid);
        tcsetpgrp(STDIN_FILENO, shell_pgid);

        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTSTP, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
    }

    char* line_buf = NULL;
    size_t line_cap = 0;
    ssize_t nread;

    for (;;) {
        if (is_interactive) {
            fprintf(stderr, "\033[94msh\033[0m:\033[32m%s\033[0m> ", cwd);
        }

        nread = getline(&line_buf, &line_cap, input);
        if (nread == -1) {
            break;
        }

        if (nread > 0 && line_buf[nread - 1] == '\n') {
            line_buf[nread - 1] = '\0';
        }

        if (line_buf[0] != '\0') {
            history_push(line_buf);
        }

        char** argv;
        int argc = split_args(line_buf, &argv);

        last_status = execute(argc, argv);

        free(argv);
    }

    if (input != stdin) {
        fclose(input);
    }

    return EXIT_SUCCESS;
}
