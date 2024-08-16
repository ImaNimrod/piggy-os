#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRAM_NAME "mkdir"

static void print_error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... DIRECTORY...\n\nCreate the DIRECTORY(ies), if they do not already exist.\n\n-p\tcreate parent directories\n-h\tdisplay this help and exit\n");
}

static void create_directory(const char* path, bool create_parents) {
    if (mkdir(path) < 0) {
        if (create_parents && errno == EEXIST) {
            struct stat st;
            if (stat(path, &st) < 0) {
                fprintf(stderr, PROGRAM_NAME ": unable to create directory: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (!S_ISDIR(st.st_mode)) {
                errno = EEXIST;
                perror(PROGRAM_NAME);
                exit(EXIT_FAILURE);
            }
        } else if (create_parents && errno == ENOENT) {
            char* parent_name = strdup(path);
            if (!parent_name) {
                exit(EXIT_FAILURE);
            }

            char* parent_dirname = dirname(parent_name);

            create_directory(parent_dirname, create_parents);
            free(parent_name);
            create_directory(path, create_parents);
        } else {
            perror(PROGRAM_NAME);
            exit(EXIT_FAILURE);
        }
    } 
}

int main(int argc, char** argv) {
    bool create_parents = false;

    int c;
    while ((c = getopt(argc, argv, "hp")) != -1) {
        switch (c) {
            case 'p':
                create_parents = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        print_error();
        return EXIT_FAILURE;
    }

    for (int i = optind; i < argc; i++) {
        create_directory(argv[i], create_parents);
    }

    return EXIT_SUCCESS;
}
