#include <dirent.h> 
#include <errno.h> 
#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 

#define PROGRAM_NAME "ls"

static void print_error(void) {
    fputs("try " PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("Usage: " PROGRAM_NAME " [OPTION]... [+FORMAT]\n" PROGRAM_NAME " [MMDDhhmm[.ss]]\nPrint the time and date in the given FORMAT or set the date and time with [MMDDhhmm[.ss]].\n\n-R\toutput date and time in RFC 5322 format\n-h\tdisplay this help and exit\n");
}

static void list_directory(char* path) {
    DIR* d = opendir(path);
    if (!d) {
        perror(PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    struct dirent* ent = readdir(d);
    while (ent != NULL) {
        puts(ent->d_name);
        puts("  ");

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
    bool list_all = false, list_almost_all = false;

    int c;
    while ((c = getopt(argc, argv ,"ahA") != -1)) {
        switch (c) {
            case 'a':
                list_all = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case 'A':
                list_almost_all = true;
                break;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        for (char** arg = argv + optind; *arg; arg++) {
            list_directory(*arg);
        }
    } else {
        list_directory(".");
    }

    return EXIT_SUCCESS;
}
