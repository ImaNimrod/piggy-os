#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum range_type {
    NONE,
    BYTES,
    FIELDS,
};

struct range {
    ssize_t start;
    ssize_t end;
};

static char input_delimiter = '\t';
static char output_delimiter = '\n';

static bool cut_bytes(FILE* fp, struct range* range) {
    char* line = NULL;
    size_t linecap = 0;
    ssize_t len;

    while ((len = getline(&line, &linecap, fp)) != -1) {
        if (len > 0 && line[len - 1] == '\n') {
            len--;
        }

        if (range->start > len) {
            continue;
        }

        ssize_t end = range->end;
        if (end > len) {
            end = len;
        }

        fwrite(line + range->start - 1, 1, end - range->start + 1, stdout);
        putchar(output_delimiter);
    }

    free(line);
    return true;
}

static bool cut_fields(FILE* fp, struct range* range, bool only_delimited) {
    char* line = NULL;
    size_t linecap = 0;
    ssize_t len;

    while ((len = getdelim(&line, &linecap, input_delimiter, fp)) != -1) {
        bool has_delimiter = strchr(line, input_delimiter) != NULL;
        if (only_delimited && !has_delimiter) {
            continue;
        }

        if (len > 0 && line[len - 1] == input_delimiter) {
            len--;
        }

        ssize_t field = 1;
        ssize_t field_start = 0;

        bool printed = false;

        for (ssize_t i = 0; i <= len; i++) {
            if (i == len || line[i] == input_delimiter) {
                ssize_t field_end = i;

                if (field >= range->start && field <= range->end) {
                    if (printed) {
                        putchar(input_delimiter);
                    }

                    fwrite(line + field_start, 1, field_end - field_start, stdout);
                    printed = true;
                }

                field++;

                if (i < len) {
                    field_start = i + 1;
                }
            }
        }

        putchar(output_delimiter);
    }

    free(line);
    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: cut -b LIST | -f LIST [-d DELIMETER] [-sz] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    enum range_type range_type = NONE;
    struct range range = {};

    bool only_delimited = false;

    int c;
    while ((c = getopt(argc, argv, "b:d:f:sz")) != -1) {
        switch (c) {
            case 'b':
                range_type = BYTES;
                range.start = 1;
                range.end = 7;
                break;
            case 'd':
                if (strlen(optarg) != 1) {
                    warnx("field delimiter must be a single character");
                    usage();
                }

                input_delimiter = optarg[0];
                break;
            case 'f':
                range_type = FIELDS;
                range.start = 1;
                range.end = 7;
                break;
            case 's':
                only_delimited = true;
                break;
            case 'z':
                input_delimiter = '\0';
                output_delimiter = '\0';
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (range_type == NONE) {
        warnx("you must specifiy list of bytes or fields");
        usage();
    }

    if (range_type != FIELDS) {
        if (only_delimited) {
            warnx("supressing non-delimited lines only makes sense when operating on fields");
        } else if (input_delimiter != '\t') {
            warnx("specifying an input delimiter only makes sense when operating on fields");
        }

        usage();
    }

    int ret = EXIT_SUCCESS;

    char* filename;
    FILE* fp;

    int i = 0;
    while (argv[i] || i == 0) {
        if (!argv[i] || strcmp(argv[i], "-") == 0) {
            filename = "stdin";
            fp = stdin;
        } else {
            filename = argv[i];
            fp = fopen(filename, "rb");
        }

        if (!fp) {
            warn("cannot open '%s'", filename);
            ret = EXIT_FAILURE;
        } else {
            switch (range_type) {
                case BYTES:
                    if (!cut_bytes(fp, &range)) {
                        ret = EXIT_FAILURE;
                    }
                    break;
                case FIELDS:
                    if (!cut_fields(fp, &range, only_delimited)) {
                        ret = EXIT_FAILURE;
                    }
                    break;
                default:
                    __builtin_unreachable();
            }

            if (fp != stdin) {
                fclose(fp);
            }
        }

        if (!argv[i]) {
            break;
        }

        i++;
    }

    return ret;
}
