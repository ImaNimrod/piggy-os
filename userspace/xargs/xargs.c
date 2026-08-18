#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_MAX_ARGS 128

static char* DEFAULT_COMMAND[] = { "echo", NULL };

static inline void* xreallocarray(void* ptr, size_t count, size_t size) {
    void* ret = reallocarray(ptr, count, size);
    if (!ret) {
        err(EXIT_FAILURE, "reallocarray");
    }
    return ret;
}

static inline char* xstrdup(const char* str) {
    char* ret = strdup(str);
    if (!ret) {
        err(EXIT_FAILURE, "strdup");
    }
    return ret;
}

static void print_command(char** argv, size_t argc) {
    for (size_t i = 0; i < argc; i++) {
        if (i != 0) {
            fputc(' ', stderr);
        }

        bool quote = false;

        for (const char* p = argv[i]; *p; p++) {
            unsigned char c = (unsigned char) *p;

            if (!isalnum(c) && c != '_' && c != '-' && c != '.' && c != '/' && c != ':') {
                quote = true;
                break;
            }
        }

        if (!quote) {
            fputs(argv[i], stderr);
            continue;
        }

        fputc('\'', stderr);

        for (const char* p = argv[i]; *p; p++) {
            if (*p == '\'') {
                fputs("'\\''", stderr);
            } else {
                fputc(*p, stderr);
            }
        }

        fputc('\'', stderr);
    }

    fputc('\n', stderr);
}

static int run_command(char** argv, size_t argc, bool verbose) {
    if (argc == 0) {
        return 0;
    }

    if (verbose) {
        print_command(argv, argc);
    }

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

    for (;;) {
        pid_t ret = waitpid(pid, &status, 0);
        if (ret == pid) {
            break;
        }

        if (ret < 0 && errno == EINTR) {
            continue;
        }

        warn("waitpid");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: xargs [-0v] [-n NUM] [COMMAND]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char delimiter = '\n';
    size_t max_args = DEFAULT_MAX_ARGS;
    bool verbose = false;

    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "0n:v")) != -1) {
        switch (c) {
            case '0':
                delimiter = '\0';
                break;
            case 'n':
                errno = 0;

                uintmax_t value = strtoumax(optarg, &end_ptr, 10);
                if (errno != 0 || end_ptr == optarg || *end_ptr || value == 0 || value > SIZE_MAX) {
                    warnx("invalid argument limit: '%s'", optarg);
                    usage();
                }

                max_args = (size_t) value;
                break;
            case 'v':
                verbose = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    char** command;
    size_t command_argc;

    if (argc == 0) {
        command = DEFAULT_COMMAND;
        command_argc = 1;
    } else {
        command = argv;
        command_argc = (size_t) argc;
    }

    size_t capacity = 32;
    size_t nargs = command_argc;

    char** args = malloc(capacity * sizeof(char*));
    if (!args) {
        err(EXIT_FAILURE, "malloc");
    }

    for (size_t i = 0; i < command_argc; i++) {
        args[i] = command[i];
    }

    char* record = NULL;
    size_t record_capacity = 0;
    size_t record_length = 0;

    int ret = EXIT_SUCCESS;

    for (;;) {
        int ch = getchar();

        if (ch == EOF || ch == delimiter) {
            bool have_record = record_length != 0 || ch == delimiter;
            if (have_record) {
                if (record == NULL) {
                    record = xreallocarray(NULL, 1, 1);
                }

                record[record_length] = '\0';

                if (nargs > command_argc && nargs - command_argc >= max_args) {
                    args = xreallocarray(args, nargs + 1, sizeof(char*));
                    args[nargs] = NULL;

                    int rc = run_command(args, nargs, verbose);
                    if (rc != 0) {
                        ret = rc;
                        goto end;
                    }

                    nargs = command_argc;
                }

                if (nargs + 1 >= capacity) {
                    capacity *= 2;
                    args = xreallocarray(args, capacity, sizeof(char*));
                }

                args[nargs++] = xstrdup(record);

                record_length = 0;
            }

            if (ch == EOF) {
                break;
            }

            continue;
        }

        if (record_length + 1 >= record_capacity) {
            size_t new_capacity = record_capacity ? record_capacity * 2 : 32;

            record = realloc(record, new_capacity);
            if (!record) {
                errx(EXIT_FAILURE, "realloc");
            }

            record_capacity = new_capacity;
        }

        record[record_length++] = (char)ch;
    }

    if (nargs > command_argc) {
        args = xreallocarray(args, nargs + 1, sizeof(char*));
        args[nargs] = NULL;

        int rc = run_command(args, nargs, verbose);
        if (rc != 0) {
            ret = rc;
        }
    }

end:
    for (size_t i = command_argc; i < nargs; i++) {
        free(args[i]);
    }

    free(args);
    free(record);

    return ret;
}
