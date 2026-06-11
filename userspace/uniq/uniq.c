#include <ctype.h>
#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool duplicate_only = false;
static bool ignore_case = false;
static bool unique_only = false;

static inline void emit(const char* line, size_t len, size_t count, FILE* fp) {
    if ((!duplicate_only && !unique_only) || (unique_only && count == 1) || (duplicate_only && count > 1)) {
        fwrite(line, 1, len, fp);
    }
}

static bool equals(const char* str1, const char* str2, size_t len) {
    if (!ignore_case) {
        return memcmp(str1, str2, len) == 0;
    }

    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char) str1[i]) != tolower((unsigned char) str2[i])) {
            return false;
        }
    }

    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: uniq [-diuz] [INPUT [OUTPUT]]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    char delimiter = '\n';

    int c;
    while ((c = getopt(argc, argv, "diuz")) != -1) {
        switch (c) {
            case 'd':
                duplicate_only = true;
                break;
            case 'i':
                ignore_case = true;
                break;
            case 'u':
                unique_only = true;
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

    if (argc > 2) {
        warnx("extra operands provided");
        usage();
    }

    char* in_filename;
    FILE* in_fp;

    if (argc < 1) {
        in_filename = "stdin";
        in_fp = stdin;
    } else {
        in_filename = argv[0];
        in_fp = fopen(in_filename, "r");
        if (in_fp == NULL) {
            err(EXIT_FAILURE, in_filename);
        }
    }

    FILE* out_fp;

    if (argc < 2) {
        out_fp = stdout;
    } else {
        out_fp = fopen(argv[1], "w");
        if (out_fp == NULL) {
            if (in_fp != stdin) {
                fclose(in_fp);
            }

            err(EXIT_FAILURE, "%s", argv[1]);
        }
    }

    char* prev = NULL;
    size_t prev_cap = 0;
    size_t prev_len = 0;

    char* line = NULL;
    size_t n = 0;

    size_t count = 0;

    ssize_t nread;
    while ((nread = getdelim(&line, &n, delimiter, in_fp)) != -1) {
        if (prev == NULL) {
            if (prev_cap < (size_t) nread) {
                char* tmp = realloc(prev, nread);
                if (tmp == NULL) {
                    err(EXIT_FAILURE, "realloc");
                }

                prev = tmp;
                prev_cap = nread;
            }

            memcpy(prev, line, nread);
            prev_len = nread;
            count = 1;
            continue;
        }

        if (prev_len == (size_t) nread && equals(prev, line, prev_len)) {
            count++;
        } else {
            emit(prev, prev_len, count, out_fp);

            if (prev_cap < (size_t) nread) {
                char* tmp = realloc(prev, nread);
                if (tmp == NULL) {
                    err(EXIT_FAILURE, "realloc");
                }

                prev = tmp;
                prev_cap = nread;
            }

            memcpy(prev, line, nread);
            prev_len = nread;
            count = 1;
        }
    }

    if (ferror(in_fp)) {
        err(EXIT_FAILURE, "getdelim(%s)", in_filename);
    }

    if (prev != NULL) {
        emit(prev, prev_len, count, out_fp);
    }

    free(prev);
    free(line);

    if (in_fp != stdin) {
        fclose(in_fp);
    }
    if (out_fp != stdout) {
        fclose(out_fp);
    }

    return EXIT_SUCCESS;
}
