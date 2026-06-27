#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char DEFAULT[] = { 'y', '\n' };

static void usage(void) {
    fprintf(stderr, "usage: yes [STRING]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    char* out = DEFAULT;
    size_t out_len = sizeof(DEFAULT);

    if (argc >= 1) {
        size_t total = 2;

        for (int i = 0; i < argc; i++) {
            total += strlen(argv[i]);
            if (argv[i + 1]) {
                total += 1;
            }
        }

        char* result = malloc(total);
        if (!result) {
            errx(EXIT_FAILURE, "malloc");
        }

        size_t offset = 0;
        for (int i = 0; i < argc; i++) {
            offset += sprintf(result + offset, "%s", argv[i]);
            if (argv[i + 1]) {
                result[offset++] = ' ';
            }
        }

        result[offset++] = '\n';
        result[offset] = '\0';

        out = result;
        out_len = offset;
    };

    size_t left = out_len;

    ssize_t nread;
    while ((nread = write(STDOUT_FILENO, out + (out_len - left), left)) > 0) {
        if ((left -= nread) == 0) {
            left = out_len;
        }
    }

    err(EXIT_FAILURE, "write(stdout)");
}
