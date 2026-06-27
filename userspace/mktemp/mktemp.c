#include <err.h>
#include <locale.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_INPUT_TEMPLATE "tmp.XXXXXX"

static char* join_paths(const char* a, const char* b) {
    size_t a_len = strlen(a);
    bool has_slash = (a_len && a[a_len-1] == '/') || b[0] == '/';

    char* result = NULL;

    int ret = has_slash ? asprintf(&result, "%s%s", a, b) : asprintf(&result, "%s/%s", a, b);
    if (ret < 0) {
        err(EXIT_FAILURE, "asprintf");
    }

    return result;
}

static void usage(void) {
    fprintf(stderr, "usage: mktemp [-dq] [-p [TMPDIR]] [TEMPLATE]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    bool directory = false;
    bool quiet = false;
    bool rooted = false;

    char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || *tmpdir == '\0') {
        tmpdir = _PATH_TMP;
    }

    int c;
    while ((c = getopt(argc, argv, "dp:q")) != -1) {
        switch (c) {
            case 'd':
                directory = true;
                break;
            case 'p':
                rooted = true;
                tmpdir = optarg;
                break;
            case 'q':
                quiet = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    char* input_template;
    if (argc == 1) {
        input_template = argv[0];
    } else {
        input_template = DEFAULT_INPUT_TEMPLATE;
        rooted = true;
    }

    if (rooted && strchr(input_template, '/')) {
        errx(EXIT_FAILURE, "template cannot contain directory separators");
    }

    char* template;
    if (rooted) {
        template = join_paths(tmpdir, input_template);
    } else {
        template = strdup(input_template);
        if (!template) {
            err(EXIT_FAILURE, "strdup");
        }
    }

    size_t template_len = strlen(template);
    if (template_len >= PATH_MAX) {
        errx(EXIT_FAILURE, "template exceeds maximum path length");
    }

    size_t suffix_len = 0;
    while (suffix_len < template_len && template[template_len - suffix_len - 1] != 'X') {
        suffix_len++;
    }

    if (INT_MAX < suffix_len) {
        errx(EXIT_FAILURE, "suffix is too long");
    }

    size_t xcount = 0;
    while (suffix_len + xcount < template_len && template[template_len - suffix_len - xcount - 1] == 'X') {
        xcount++;
    }

    if (xcount < 6) {
        errx(EXIT_FAILURE, "too few X's in template: %s", template);
    }

    if (directory) {
        if (suffix_len != 0) {
            if (!quiet) {
                warn("suffixes are unsupported with directories: '%s'", template);
            }

            free(template);
            return EXIT_FAILURE;
        }

        if (!mkdtemp(template)) {
            if (!quiet) {
                warn("mkdtemp");
            }

            free(template);
            return EXIT_FAILURE;
        }
    } else {
        int fd = mkstemps(template, (int) suffix_len);
        if (fd < 0) {
            if (!quiet) {
                warn("mkstemps");
            }

            free(template);
            return EXIT_FAILURE;
        }

        close(fd);
    }

    puts(template);
    free(template);

    return EXIT_SUCCESS;
}
