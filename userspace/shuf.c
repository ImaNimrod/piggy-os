#include <err.h> 
#include <errno.h> 
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_LINE_COUNT 4

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static char delimiter = '\n';
static size_t print_line_count = SIZE_MAX;

static int do_shuf(const char* filename, FILE* fp) {
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

        warn(filename);
        return EXIT_FAILURE;
    }

    srandom(time(NULL));

    // quick Fisher-Yates shuffle
    for (size_t i = line_count - 1; i > 0; i--) {
        size_t j = (((size_t) random() << 32) | random()) % (i + 1);

        char* tmp = lines[j];
        lines[j] = lines[i];
        lines[i] = tmp;
    }

    print_line_count = (print_line_count == SIZE_MAX) ? line_count : MIN(print_line_count, line_count);

    for (size_t i = 0; i < print_line_count; i++) {
        puts(lines[i]);
    }

    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: shuf [-z] [-n COUNT] [FILE]\n"
            "With no FILE, or when FILE is -, read from stdin.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "n:z")) != -1) {
        switch (c) {
            case 'n':
                errno = 0;
                print_line_count = (size_t) strtoull(optarg, &end_ptr, 10);
                if (errno != 0 || optarg == end_ptr) {
                    warnx("invalid line count: '%s'", optarg);
                    usage();
                }
                break;
            case 'z':
                delimiter = '\0';
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    if (print_line_count == 0) {
        return EXIT_SUCCESS;
    }

    char* filename;
    FILE* fp;

    if (argv[0] == NULL || strcmp(argv[0], "-") == 0) {
        fp = stdin;
        filename = "stdin";
    } else {
        fp = fopen(argv[0], "r");
        filename = argv[0];
    }

    if (fp == NULL) {
        err(EXIT_FAILURE, filename);
    }

    int ret = do_shuf(filename, fp);

    if (fp != stdin) {
        fclose(fp);
    }

    return ret;
}
