#include <sys/param.h> 

#include <err.h> 
#include <errno.h> 
#include <inttypes.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_LINE_COUNT 4

static char delimiter = '\n';
static uintmax_t print_line_count = UINTMAX_MAX;
static bool repeat = false;

static void fisher_yates(char** lines, size_t line_count);
static size_t random_uniform(size_t n);

static int do_shuf_args(int argc, char** argv) {
    if (argc <= 0) {
        return EXIT_SUCCESS;
    }

    if (!repeat) {
        size_t limit = (print_line_count == UINTMAX_MAX) ? (size_t) argc : (size_t) print_line_count;

        fisher_yates(argv, argc);

        size_t to_print = MIN(limit, argc);
        for (size_t i = 0; i < to_print; i++) {
            printf("%s%c", argv[i], delimiter);
        }

        return EXIT_SUCCESS;
    }

    if (print_line_count == UINTMAX_MAX) {
        for (;;) {
            printf("%s%c", argv[random_uniform(argc)], delimiter);
        }
    } else {
        for (size_t i = 0; i < print_line_count; i++) {
            printf("%s%c", argv[random_uniform(argc)], delimiter);
        }
    }

    return EXIT_SUCCESS;
}

static int do_shuf_file(const char* filename, FILE* fp) {
    size_t line_capacity = INITIAL_LINE_COUNT;
    size_t line_count = 0;

    char** lines = malloc(line_capacity * sizeof(char*));
    if (!lines) {
        errx(EXIT_FAILURE, "malloc");
    }

    char* line = NULL;
    size_t n = 0;

    ssize_t nread;
    while ((nread = getdelim(&line, &n, delimiter, fp)) != -1) {
        if (line_count >= line_capacity) {
            line_capacity *= 2;

            lines = reallocarray(lines, line_capacity, sizeof(char*));
            if (!lines) {
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

    if (line_count == 0) {
        free(lines);
        return EXIT_SUCCESS;
    }

    if (!repeat) {
        size_t limit = (print_line_count == UINTMAX_MAX) ? line_count : (size_t) print_line_count;

        fisher_yates(lines, line_count);

        size_t to_print = MIN(limit, line_count);
        for (size_t i = 0; i < to_print; i++) {
            fputs(lines[i], stdout);
        }

        goto end;
    }

    if (print_line_count == UINTMAX_MAX) {
        for (;;) {
            fputs(lines[random_uniform(line_count)], stdout);
        }
    } else {
        for (size_t i = 0; i < print_line_count; i++) {
            fputs(lines[random_uniform(line_count)], stdout);
        }
    }

end:
    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);

    return EXIT_SUCCESS;
}

static void fisher_yates(char** lines, size_t line_count) {
    for (size_t i = line_count - 1; i > 0; i--) {
        size_t j = random_uniform(i + 1);
        char* tmp = lines[j];
        lines[j] = lines[i];
        lines[i] = tmp;
    }
}

static size_t random_uniform(size_t n) {
    long limit = RAND_MAX - (RAND_MAX % n);

    long r;
    do {
        r = random();
    } while (r >= limit);

    return (size_t) (r % n);
}

static void seed_random(void) {
    srandom(time(NULL) ^ getpid());
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

                print_line_count = strtoumax(optarg, &end_ptr, 10);
                if (errno != 0 || end_ptr == optarg || *end_ptr) {
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
        seed_random();
        return do_shuf_args(argc, argv);
    }

    char* filename;
    FILE* fp;

    if (!argv[0] || strcmp(argv[0], "-") == 0) {
        filename = "stdin";
        fp = stdin;
    } else {
        filename = argv[0];
        fp = fopen(filename, "r");
        if (!fp) {
            err(EXIT_FAILURE, filename);
        }
    }

    seed_random();

    int ret = do_shuf_file(filename, fp);

    if (fp != stdin) {
        fclose(fp);
    }

    return ret;
}
