#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <signal.h> 
#include <string.h> 
#include <termios.h> 
#include <unistd.h> 

#include "builtins.h"
#include "history.h"
#include "interactive.h"
#include "sh.h"
#include "tokenizer.h"

struct command {
    char** argv;
    size_t argc;
};

struct pipeline {
    struct command* commands;
    size_t count;
};

int last_status;

static FILE* input;
static bool is_interactive;
static struct termios old_termios;
static pid_t shell_pgid;

static void free_pipeline(struct pipeline* pipeline) {
    for (size_t i = 0; i < pipeline->count; i++) {
        free(pipeline->commands[i].argv);
    }

    free(pipeline->commands);
}

static int parse_pipeline(struct token* tokens, size_t token_count, struct pipeline* pipeline) {
    memset(pipeline, 0, sizeof(*pipeline));

    size_t command_capacity = 4;

    pipeline->commands = malloc(command_capacity * sizeof(*pipeline->commands));
    if (!pipeline->commands) {
        return -1;
    }

    size_t command_start = 0;

    for (size_t i = 0; i <= token_count; i++) {
        bool end = i == token_count;
        bool pipe = !end && tokens[i].type == TOKEN_PIPE;

        if (!end && !pipe) {
            continue;
        }

        if (i == command_start) {
            warnx("empty command in pipeline");
            free_pipeline(pipeline);
            return -1;
        }

        if (pipeline->count >= command_capacity) {
            command_capacity *= 2;

            struct command* commands = realloc(pipeline->commands, command_capacity * sizeof(*pipeline->commands));
            if (!commands) {
                free_pipeline(pipeline);
                return -1;
            }

            pipeline->commands = commands;
        }

        struct command* command = &pipeline->commands[pipeline->count++];
        command->argc = i - command_start;

        command->argv = malloc((command->argc + 1) * sizeof(char*));
        if (!command->argv) {
            free_pipeline(pipeline);
            return -1;
        }

        for (size_t j = 0; j < command->argc; j++) {
            command->argv[j] = tokens[command_start + j].text;
        }

        command->argv[command->argc] = NULL;

        command_start = i + 1;
    }

    return 0;
}

static void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios) < 0) {
        err(EXIT_FAILURE, "tcsetattr");
    }
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &old_termios) < 0) {
        err(EXIT_FAILURE, "tcgetattr");
    }

    struct termios new_termios = old_termios;
    new_termios.c_iflag &= ~(BRKINT | ICRNL | ISTRIP | IXON);
    new_termios.c_oflag &= ~(OPOST);
    new_termios.c_cflag |= CS8;
    new_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) < 0) {
        err(EXIT_FAILURE, "tcsetattr");
    }
}

static int run_program(char** argv) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        if (setpgid(0, 0) < 0) {
            err(EXIT_FAILURE, "setpgid");
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        execvp(argv[0], argv);
        err(EXIT_FAILURE, "execvp");
    }

    if (setpgid(pid, pid) < 0) {
        warn("setpgid");
    }

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    int status;
    pid_t ret;

    do {
        ret = waitpid(pid, &status, 0);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        warn("waitpid");

        if (is_interactive) {
            tcsetpgrp(STDIN_FILENO, shell_pgid);
        }

        return EXIT_FAILURE;
    }

    if (is_interactive) {
        if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) {
            warn("tcsetpgrp");
        }
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);

        if (is_interactive) {
            if (sig == SIGINT) {
                fputc('\n', stdout);
            } else {
                warnx("%s", strsignal(sig));
            }
        }

        return 128 + sig;
    }

    return WEXITSTATUS(status);
}

static int execute_builtin(int argc, char** argv) {
    size_t i = 0;

    while (BUILTINS[i].name) {
        if (strcmp(argv[0], BUILTINS[i].name) == 0) {
            return (*BUILTINS[i].func)(argc, argv);
        }

        i++;
    }

    return -1;
}

static int execute(int argc, char** argv) {
    if (argc == 0) {
        return EXIT_SUCCESS;
    }

    int builtin = execute_builtin(argc, argv);
    if (builtin >= 0) {
        return builtin;
    }

    return run_program(argv);
}

static int execute_pipeline(struct pipeline* pipeline) {
    size_t count = pipeline->count;
    if (count == 0) {
        return EXIT_SUCCESS;
    }

    if (count == 1) {
        return execute(pipeline->commands[0].argc, pipeline->commands[0].argv);
    }

    int (*pipes)[2] = calloc(count - 1, sizeof(*pipes));
    if (!pipes) {
        warn("calloc");
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            warn("pipe");

            for (size_t j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            free(pipes);
            return EXIT_FAILURE;
        }
    }

    int pgid_pipe[2] = { -1, -1 };

    if (is_interactive) {
        if (pipe(pgid_pipe) < 0) {
            warn("pipe");

            for (size_t i = 0; i < count - 1; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }

            free(pipes);
            return EXIT_FAILURE;
        }
    }

    pid_t* pids = calloc(count, sizeof(*pids));
    if (!pids) {
        warn("calloc");

        for (size_t i = 0; i < count - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        if (is_interactive) {
            close(pgid_pipe[0]);
            close(pgid_pipe[1]);
        }

        free(pipes);
        return EXIT_FAILURE;
    }

    pid_t pgid = 0;
    size_t spawned = 0;

    for (size_t i = 0; i < count; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            warn("fork");
            goto error;
        }

        if (pid == 0) {
            if (is_interactive) {
                close(pgid_pipe[1]);
            }

            pid_t child_pgid = pgid;

            if (child_pgid == 0) {
                child_pgid = getpid();
            }

            if (setpgid(0, child_pgid) < 0) {
                err(EXIT_FAILURE, "setpgid");
            }

            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    err(EXIT_FAILURE, "dup2");
                }
            }

            if (i < count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    err(EXIT_FAILURE, "dup2");
                }
            }

            for (size_t j = 0; j < count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (is_interactive) {
                char dummy;
                if (read(pgid_pipe[0], &dummy, 1) < 0) {
                    _Exit(EXIT_FAILURE);
                }

                close(pgid_pipe[0]);
            }

            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            execvp(pipeline->commands[i].argv[0], pipeline->commands[i].argv);
            err(EXIT_FAILURE, "%s", pipeline->commands[i].argv[0]);
        }

        pids[i] = pid;
        spawned++;

        if (pgid == 0) {
            pgid = pid;
        }

        if (setpgid(pid, pgid) < 0) {
            if (errno != ESRCH) {
                warn("setpgid");
                goto error;
            }
        }
    }

    for (size_t i = 0; i < count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    free(pipes);
    pipes = NULL;

    if (is_interactive) {
        if (tcsetpgrp(STDIN_FILENO, pgid) < 0) {
            warn("tcsetpgrp");
            goto error;
        }

        close(pgid_pipe[1]);
        pgid_pipe[1] = -1;

        close(pgid_pipe[0]);
        pgid_pipe[0] = -1;
    }

    int last_pipeline_status = EXIT_SUCCESS;

    for (size_t i = 0; i < count; i++) {
        pid_t ret;
        int status;

        do {
            ret = waitpid(pids[i], &status, 0);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            warn("waitpid");

            if (i == count - 1) {
                last_pipeline_status = EXIT_FAILURE;
            }

            continue;
        }

        if (i == count - 1) {
            if (WIFEXITED(status)) {
                last_pipeline_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);

                last_pipeline_status = 128 + sig;

                if (is_interactive) {
                    if (sig == SIGINT) {
                        fputc('\n', stdout);
                    } else {
                        warnx("%s", strsignal(sig));
                    }
                }
            }
        }
    }

    if (is_interactive) {
        if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) {
            err(EXIT_FAILURE, "tcsetpgrp");
        }
    }

    free(pids);

    return last_pipeline_status;

error:
    if (pipes) {
        for (size_t i = 0; i < count - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        free(pipes);
    }

    if (is_interactive) {
        if (pgid_pipe[1] != -1) {
            close(pgid_pipe[1]);
            pgid_pipe[1] = -1;
        }

        if (pgid_pipe[0] != -1) {
            close(pgid_pipe[0]);
            pgid_pipe[0] = -1;
        }
    }

    if (pgid != 0) {
        kill(-pgid, SIGTERM);
    }

    for (size_t i = 0; i < spawned; i++) {
        while (waitpid(pids[i], NULL, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }
    }

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    free(pids);

    return EXIT_FAILURE;
}

static void usage(void) {
    fprintf(stderr, "usage: sh [-i] [-c COMMAND]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool force_interactive = false;

    int c;
    while ((c = getopt(argc, argv, "i")) != -1) {
        switch (c) {
            case 'i':
                force_interactive = true;
                break;
            default:
                usage();
        }
    }

    pwd = getenv("PWD");
    if (pwd) {
        pwd = strdup(pwd);
        if (!pwd) {
            err(EXIT_FAILURE, "strdup");
        }
    } else {
        pwd = getcwd(NULL, 0);
        if (pwd) {
            setenv("PWD", pwd, 1);
        }
    }

    input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (!input) {
            err(EXIT_FAILURE, argv[1]);
        }
    }

    is_interactive = force_interactive || isatty(fileno(input));

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
            enable_raw_mode();
            nread = readline_interactive(&line_buf);
            disable_raw_mode();
        } else {
            nread = getline(&line_buf, &line_cap, input);
        }

        if (nread < 0) {
            break;
        }

        if (nread > 0 && line_buf[nread - 1] == '\n') {
            line_buf[nread - 1] = '\0';
        }

        char* comment = strchr(line_buf, '#');
        if (comment) {
            line_buf[comment - line_buf] = '\0';
        }

        if (*line_buf == '\0') {
            continue;
        }

        history_push(line_buf);

        struct token* tokens;

        int token_count = tokenize(line_buf, &tokens);
        if (token_count < 0) {
            continue;
        }

        if (token_count == 0) {
            tokens_free(tokens, token_count);
            continue;
        }

        struct pipeline pipeline;
        if (parse_pipeline(tokens, token_count, &pipeline) < 0) {
            tokens_free(tokens, token_count);
            continue;
        }

        last_status = execute_pipeline(&pipeline);

        free_pipeline(&pipeline);
        tokens_free(tokens, token_count);
    }

    if (input != stdin) {
        fclose(input);
    }

    return EXIT_SUCCESS;
}
