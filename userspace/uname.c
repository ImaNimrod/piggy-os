#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

#define PROGRAM_NAME "uname"

enum {
    PRINT_SYSNAME   = (1 << 0),
    PRINT_NODENAME  = (1 << 1),
    PRINT_RELEASE   = (1 << 2),
    PRINT_VERSION   = (1 << 4),
    PRINT_MACHINE   = (1 << 8),
    PRINT_ALL = PRINT_SYSNAME | PRINT_NODENAME | PRINT_RELEASE | PRINT_VERSION | PRINT_MACHINE
};

static void print_error(void) {
    fputs("try " PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("Usage: " PROGRAM_NAME " [OPTION]...\nPrint system information. With no OPTION, same as -s.\n\n-a\tprint all information in the following order\n-s\tprint the kernel name\n-n\tprint the network hostname\n-r\tprint the kernel release\n-v\tprint the kernel version\n-m\tprint the machine hardware name\n-h\tdisplay this help and exit\n");
}

static void print_info(char* str) {
    static bool first_time = true;
    if (!first_time) {
        putchar(' ');
    }

    puts(str);
    first_time = false;
}

int main(int argc, char** argv) {
    int print_mode = 0;

    int c;
    while ((c = getopt(argc, argv, "ahmnrsv")) != -1) {
        switch (c) {
            case 'a':
                print_mode |= PRINT_ALL;
                break;
            case 's':
                print_mode |= PRINT_SYSNAME;
                break;
            case 'n':
                print_mode |= PRINT_NODENAME;
                break;
            case 'r':
                print_mode |= PRINT_RELEASE;
                break;
            case 'v':
                print_mode |= PRINT_VERSION;
                break;
            case 'm':
                print_mode |= PRINT_MACHINE;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (print_mode == 0) {
        print_mode = PRINT_SYSNAME;
    }

    if (optind < argc) {
        print_error();
        return EXIT_FAILURE;
    }

    struct utsname uts;
    if (utsname(&uts) < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    if (print_mode & PRINT_SYSNAME) {
        print_info(uts.sysname);
    }
    if (print_mode & PRINT_NODENAME) {
        print_info(uts.nodename);
    }
    if (print_mode & PRINT_RELEASE) {
        print_info(uts.release);
    }
    if (print_mode & PRINT_VERSION) {
        print_info(uts.version);
    }
    if (print_mode & PRINT_MACHINE) {
        print_info(uts.machine);
    }
    putchar('\n');

    return EXIT_SUCCESS;
}
