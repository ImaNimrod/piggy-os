#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char* optarg;
int opterr, optind = 1, optopt;

int getopt(int argc, char* const argv[], const char* optstring) {
    static int optchar_offset = 1;

    if (argc <= optind) {
        return -1;
    }

    char* optgroup = argv[optind];

    if (optgroup == NULL || optgroup[0] != '-' || !strcmp(optgroup, "-")) {
        return -1;
    }

    if (!strcmp(optgroup, "--")) {
        optind++;
        return -1;
    }

    char optchar = optgroup[optchar_offset];
    bool matched = false;

    size_t i;
    for (i = 0; i < strlen(optstring); ++i) {
        if (optstring[i] == optchar) {
            matched = true;
            break;
        }
    }

    if (!matched) {
        optopt = optchar;
        optind++;
        fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optchar);
        return '?';
    }

    bool takes_arg = (optstring[i + 1] == ':');
    bool terminates_optgroup = (optgroup[optchar_offset + 1] == '\0');

    if (!takes_arg) {
        if (terminates_optgroup) {
            optind++;
            optchar_offset = 1;
        } else {
            optchar_offset++;
        }
        return optchar;
    }

    bool arg_exists = (argc >= optind + 2);

    if (terminates_optgroup) {
        if (arg_exists) {
            optarg = argv[optind + 1];
            optind += 2;
        } else {
            optind++;
            optarg = NULL;
            optopt = optchar;
            fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optchar);
            return ':';
        }
    } else {
        optarg = optgroup + optchar_offset + 1;
        optind++;
        optchar_offset = 1;
    }

    return optchar;
}
