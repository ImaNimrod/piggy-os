#include <dirent.h> 
#include <errno.h> 
#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 

#define PROGRAM_NAME "ls"

static bool list_all = false;
static bool list_almost_all = false;

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [FILE]...\n\nList information about the FILEs (the current directory by default).\n\n-a\tdo not ignore entries starting with .\n-A\tdo not list implied . and .. directory entries\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static void list_directory(char* path) {
    DIR* d = opendir(path);
    if (!d) {
        perror(PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    struct dirent* ent = readdir(d);
    while (ent != NULL) {
        if (*ent->d_name == '.') {
            if (!list_all & !list_almost_all) {
                goto next;
            }

            if (list_almost_all && strlen(ent->d_name) <= 2) {
                goto next;
            }
        }

        puts(ent->d_name);
        if (ent->d_type == DT_DIR) {
            putchar('/');
        }
        puts("  ");

next:
        ent = readdir(d);
        if (errno != 0) {
            perror(PROGRAM_NAME);
            exit(EXIT_FAILURE);
        }
    }
    putchar('\n');

    closedir(d);
}

int main(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "ahA")) != -1) {
        switch (c) {
            case 'a':
                list_all = true;
                break;
            case 'A':
                if (!list_all) {
                    list_almost_all = true;
                }
                break;
            case 'h':
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    if (optind == argc - 1) {
        list_directory(argv[argc - 1]);
    } else if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            printf("%s:\n", argv[i]);
            list_directory(argv[i]);
            putchar('\n');
        }
    } else {
        list_directory(".");
    }

    return EXIT_SUCCESS;
}
