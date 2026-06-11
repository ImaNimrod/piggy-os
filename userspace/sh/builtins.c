#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "history.h"
#include "sh.h"

extern char** environ;

static int builtin_cd(int argc, char* argv[]);
static int builtin_echo(int argc, char* argv[]);
static int builtin_exec(int argc, char* argv[]);
static int builtin_exit(int argc, char* argv[]);
static int builtin_export(int argc, char* argv[]);
static int builtin_history(int argc, char* argv[]);
static int builtin_pwd(int argc, char* argv[]);
static int builtin_source(int argc, char* argv[]);
static int builtin_unset(int argc, char* argv[]);

const struct shell_builtin BUILTINS[] = {
    { "cd", builtin_cd },
    { "echo", builtin_echo },
    { "exec", builtin_exec },
    { "exit", builtin_exit },
    { "export", builtin_export },
    { "history", builtin_history },
    { "pwd", builtin_pwd },
    { "source", builtin_source },
    { "unset", builtin_unset },
    { NULL, NULL },
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

static int builtin_cd(int argc, char* argv[]) {
    (void) argc;

    const char* arg = argv[1];
    const char* path;

    char buf[PATH_MAX];

    if (arg == NULL) {
        path = getenv("HOME");
    } else if (arg[0] == '~') {
        const char* home = getenv("HOME");
        if (home == NULL) {
            warnx("$HOME not set");
            return EXIT_FAILURE;
        }

        if (arg[1] == '\0') {
            path = home;
        } else if (arg[1] == '/') {
            snprintf(buf, sizeof(buf), "%s%s", home, arg + 1);
            path = buf;
        } else {
            path = arg;
        }
    } else {
        path = arg;
    }

    if (chdir(path) < 0) {
        warn(path);
    }

    setpwd();
    return EXIT_SUCCESS;
}

static int builtin_echo(int argc, char* argv[]) {
    bool trailing_newline = true;

    int i = 1;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        trailing_newline = false;
        i++;
    }

    for (; i < argc; i++) {
        fputs(argv[i], stdout);

        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    if (trailing_newline) {
        putchar('\n');
    }

    return EXIT_SUCCESS;
}

static int builtin_exec(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    if (argc == 1) {
        return EXIT_SUCCESS;
    }

    execvp(argv[1], &argv[1]);
    warn(argv[1]);
    return EXIT_FAILURE;
}

static int builtin_exit(int argc, char* argv[]) {
    (void) argc;
    (void) argv;
    exit(last_status);
}

static int builtin_export(int argc, char* argv[]) {
    if (argc == 1) {
        for (char** env = environ; *env != NULL; env++) {
            puts(*env);
        }

        return EXIT_SUCCESS;
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

    return EXIT_SUCCESS;
}

static int builtin_history(int argc, char* argv[]) {
    size_t count = 10;

    if (argc >= 2) {
        char* endptr;

        size_t tmp = strtol(argv[1], &endptr, 10);
        if (endptr == argv[1] || *endptr != '\0') {
            printf("invalid history entry count '%s'\n", argv[1]);
            return EXIT_FAILURE;
        } else {
            count = tmp;
        }
    }

    history_print(count);
    return EXIT_SUCCESS;
}

static int builtin_pwd(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    char* buf = malloc(PATH_MAX);
    if (buf == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    if (getcwd(buf, PATH_MAX) == NULL) {
        warn("getcwd");
        return EXIT_FAILURE;
    }

    puts(buf);

    free(buf);
    return EXIT_SUCCESS;
}

static int builtin_source(int argc, char* argv[]) {
    if (argc < 2) {
        warnx("source: filename required");
        return EXIT_FAILURE;
    }

    FILE* fp = fopen(argv[1], "r");
    if (fp == NULL) {
        warn(argv[1]);
        return EXIT_FAILURE;
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    int ret = EXIT_SUCCESS;

    while ((nread = getline(&line, &len, fp)) != -1) {
        char** argv;
        int argc = split_args(line, &argv);
        ret |= execute(argc, argv);
        free(argv);
    }

    free(line);
    fclose(fp);
    return ret;
}

static int builtin_unset(int argc, char* argv[]) {
    if (argc < 1) {
        warnx("unset: missing operand");
        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;

    for (int i = 1; i < argc; i++) {
        if (!is_valid_variable(argv[i])) {
            warnx("unset: '%s' is not a valid name", argv[i]);
            ret = EXIT_FAILURE;
            continue;
        }

        if (unsetenv(argv[i]) < 0) {
            warn("unsetenv(%s)", argv[i]);
            ret = EXIT_FAILURE;
        }
    }

    return ret;
}
