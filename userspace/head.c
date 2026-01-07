#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ssize_t byte_count = -1;
static ssize_t line_count = -1;
static char line_delimiter = '\n';
static bool quiet = false;
static bool verbose = false;

static void head_bytes(FILE* fp, size_t count) {
    char buffer[4096];

    while (count > 0) {
        size_t chunk = (count < sizeof(buffer)) ? count : sizeof(buffer);

        size_t nread = fread(buffer, 1, chunk, fp);
        if (nread == 0) {
            if (feof(fp)) {
                break;
            } else {
                err(EXIT_FAILURE, "fread");
            }
        }

        size_t nwritten = fwrite(buffer, 1, nread, stdout);
        if (nwritten != nread) {
            err(EXIT_FAILURE, "fwrite(stdout)");
        }

        count -= nread;
    }
}

static void head_lines(FILE* fp, size_t count) {
    char* line = NULL;
    size_t cap  = 0;
    size_t lines_printed = 0;

    while (lines_printed < count) {
        ssize_t nread = getdelim(&line, &cap, line_delimiter, fp);
        if (nread < 0) {
            if (feof(fp)) {
                break;
            } else {
                free(line);
                err(EXIT_FAILURE, "getdelim");
            }
        }

        if (fwrite(line, 1, (size_t)nread, stdout) != (size_t)nread) {
            free(line);
            err(EXIT_FAILURE, "fwrite(stdout)");
        }

        lines_printed++;
    }

    free(line);
}

static void usage(void) {
    fprintf(stderr, "usage: head [-qvz] [-c BYTES | -n LINES |] [FILE]...\n"
            "With no FILE, or when FILE is -, read from stdin.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "c:n:qvz")) != -1) {
        switch (c) {
            case 'c':
                errno = 0;
                byte_count = strtol(optarg, &end_ptr, 10);
                if (errno != 0 || byte_count < 0 || optarg == end_ptr) {
                    warnx("invalid byte count: '%s'", optarg);
                    usage();
                }
                break;
            case 'n':
                errno = 0;
                line_count = strtol(optarg, &end_ptr, 10);
                if (errno != 0 || line_count < 0 || optarg == end_ptr) {
                    warnx("invalid line count: '%s'", optarg);
                    usage();
                }
                break;
            case 'q':
                quiet = true;
                verbose = false;
                break;
            case 'v':
                quiet = false;
                verbose = true;
                break;
            case 'z':
                line_delimiter = '\0';
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (byte_count != -1 && line_count != -1) {
        warnx("cannot combine byte and line counts");
        usage();
    }

    if (line_count == -1) {
        line_count = 10;
    }

    int ret = EXIT_SUCCESS;

    if (*argv != NULL) {
        bool first = true;
        char* path;
        FILE* fp;

        while ((path = *argv) != NULL) {
            if (path == NULL || strcmp(path, "-") == 0) {
                fp = stdin;
            } else {
                fp = fopen(path, "r");
            }

            if (fp == NULL) {
                warn("%s", path);
                ret = EXIT_FAILURE;
            } else {
                if (verbose || (!quiet && argc > 1)) {
                    printf("%c==| %s |==\n", first ? '\0' : '\n', path);
                    first = false;
                }

                if (byte_count == -1) {
                    head_lines(fp, line_count);
                } else {
                    head_bytes(fp, byte_count);
                }

                if (fp != stdin) {
                    fclose(fp);
                }
            }

            argv++;
        }
    } else {
        if (byte_count == -1) {
            head_lines(stdin, line_count);
        } else {
            head_bytes(stdin, byte_count);
        }
    }

    return ret;
}
