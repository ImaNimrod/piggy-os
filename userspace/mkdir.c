#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char* program_name;

static void print_error(void) {
    fprintf(stderr, "try '%s -h' for more information\n", program_name);
}

static void print_help(void) {
    printf("Usage: %s [OPTION]... DIRECTORY...\n \
            Create the DIRECTORY(ies), if they do not already exist.\n\n \
            -p        create parent directories\n \
            -h        display this help and exit\n", program_name);
}

static void create_directory(const char* path, bool create_parents) {
    if (mkdir(path) < 0) {
        if (create_parents && errno == EEXIST) {
            struct stat st;
            if (stat(path, &st) < 0) {
                fprintf(stderr, "%s: unable to create directory: %s\n", program_name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (!S_ISDIR(st.st_mode)) {
                errno = EEXIST;
                perror(program_name);
                exit(EXIT_FAILURE);
            }
        } else if (create_parents && errno == ENOENT) {
            char* parent_name = strdup(path);
            if (!parent_name) {
                exit(EXIT_FAILURE);
            }

            parent_name = dirname(parent_name);

            create_directory(parent_name, create_parents);
            free(parent_name);
            create_directory(path, create_parents);
        } else {
            perror(program_name);
            exit(EXIT_FAILURE);
        }
    } 
}

int main(int argc, char** argv) {
    program_name = argv[0];

    bool create_parents = false;

    int c;
    while ((c = getopt(argc, argv, "hp")) != -1) {
        switch (c) {
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case 'p':
                create_parents = true;
                break;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        print_error();
        return EXIT_FAILURE;
    }

    for (int i = optind; i < argc; i++) {
        create_directory(argv[i], create_parents);
    }

    return EXIT_SUCCESS;
}
