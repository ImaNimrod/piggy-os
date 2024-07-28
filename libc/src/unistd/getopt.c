#include <stdio.h>
#include <string.h>
#include <unistd.h>

char* optarg;
int opterr, optind = 1, optopt;

static char* optcursor = NULL;

int getopt(int argc, char* const argv[], const char* optstring) {
    int optchar = -1;
    const char* optdecl = NULL;

    optarg = NULL;
    opterr = optopt = 0;

    if (optind >= argc) {
        optcursor = NULL;
        return -1;
    }

    if (argv[optind] == NULL) {
        optcursor = NULL;
        return -1;
    }

    if (*argv[optind] != '-') {
        optcursor = NULL;
        return -1;
    }

    if (!strcmp(argv[optind], "-")) {
        optcursor = NULL;
        return -1;
    }

    if (!strcmp(argv[optind], "--")) {
        optind++;
        optcursor = NULL;
        return -1;
    }

    if (optcursor == NULL || *optcursor == '\0') {
        optcursor = argv[optind] + 1;
    }

    optchar = *optcursor;
    optopt = optchar;

    optdecl = strchr(optstring, optchar);
    if (optdecl != NULL) {
        if (optdecl[1] == ':') {
            optarg = optcursor++;

            if (*optarg == '\0') {
                if (optdecl[2] != ':') {
                    if (optind++ < argc) {
                        optarg = argv[optind];
                    } else {
                        fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optchar);
                        optarg = NULL;
                        optchar = (optstring[0] == ':') ? ':' : '?';
                    }
                } else {
                    optarg = NULL;
                }
            }

            optcursor = NULL;
        }
    } else {
        fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optchar);
        optchar = '?';
    }

    if (optcursor == NULL || *++optcursor == '\0') {
        optind++;
    }

    return optchar;
}
