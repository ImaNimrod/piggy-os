#include <sys/param.h> 

#include <err.h> 
#include <errno.h> 
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_LINE_COUNT 4

static bool repeat = false;
static char delimiter = '\n';
static size_t print_line_count = SIZE_MAX;

static void seed_rng(void);

static int do_shuf_args(int argc, char** argv) {
    if (argc <= 0) {
        return EXIT_SUCCESS;
    }

    size_t limit = (print_line_count == SIZE_MAX) ? (size_t) argc : print_line_count;
    size_t printed = 0;

    while (printed < limit) {
        seed_rng();

        for (size_t i = argc - 1; i > 0; i--) {
            size_t j = (((size_t) random() << 32) | random()) % (i + 1);

            char* tmp = argv[j];
            argv[j] = argv[i];
            argv[i] = tmp;
        }

        size_t to_print = MIN(limit - printed, (size_t) argc);

        for (size_t i = 0; i < to_print; i++) {
            printf("%s%c", argv[i], delimiter);
        }

        printed += to_print;

        if (!repeat) {
            break;
        }
    }

    return EXIT_SUCCESS;
}

static int do_shuf_file(const char* filename, FILE* fp) {
    size_t line_capacity = INITIAL_LINE_COUNT;
    size_t line_count = 0;

    char** lines = malloc(line_capacity * sizeof(char*));
    if (lines == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    char* line = NULL;
    size_t n = 0;

    ssize_t nread;
    while ((nread = getdelim(&line, &n, delimiter, fp)) != -1) {
        if (line_count >= line_capacity) {
            line_capacity *= 2;

            lines = reallocarray(lines, line_capacity, sizeof(char*));
            if (lines == NULL) {
                err(EXIT_FAILURE, "reallocarray");
            }
        }

        lines[line_count++] = line;

        line = NULL;
        n = 0;
    }

    if (ferror(fp)) {
        for (size_t i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);

        warn("%s", filename);
        return EXIT_FAILURE;
    }

    size_t limit = (print_line_count == SIZE_MAX) ? line_count : print_line_count;
    size_t printed = 0;

    while (printed < limit) {
        seed_rng();

        for (size_t i = line_count - 1; i > 0; i--) {
            size_t j = (((size_t) random() << 32) | random()) % (i + 1);

            char* tmp = lines[j];
            lines[j] = lines[i];
            lines[i] = tmp;
        }

        size_t to_print = MIN(limit - printed, line_count);

        for (size_t i = 0; i < to_print; i++) {
            printf("%s", lines[i]);
        }

        printed += to_print;

        if (!repeat) {
            break;
        }
    }

    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);

    return EXIT_SUCCESS;
}

static void seed_rng(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        err(EXIT_FAILURE, "clock_gettime(CLOCK_REALTIME)");
    }

    srandom(ts.tv_sec ^ ts.tv_nsec);
}

static void usage(void) {
    fprintf(stderr, "usage: shuf [-rz] [-n COUNT] [FILE]\n"
                    "       shuf -e [ARG]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool echo_mode = false;

    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "en:rz")) != -1) {
        switch (c) {
            case 'e':
                echo_mode = true;
                break;
            case 'n':
                errno = 0;
                print_line_count = (size_t) strtoull(optarg, &end_ptr, 10);
                if (errno != 0 || optarg == end_ptr) {
                    warnx("invalid line count: '%s'", optarg);
                    usage();
                }
                break;
            case 'r':
                repeat = true;
                break;
            case 'z':
                delimiter = '\0';
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (!echo_mode && argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    if (print_line_count == 0) {
        return EXIT_SUCCESS;
    }

    if (echo_mode) {
        return do_shuf_args(argc, argv);
    }

    char* filename;
    FILE* fp;

    if (argv[0] == NULL || strcmp(argv[0], "-") == 0) {
        filename = "stdin";
        fp = stdin;
    } else {
        filename = argv[0];
        fp = fopen(filename, "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, filename);
        }
    }

    int ret = do_shuf_file(filename, fp);

    if (fp != stdin) {
        fclose(fp);
    }

    return ret;
}
