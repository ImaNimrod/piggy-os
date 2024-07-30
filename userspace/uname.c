#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

#define PROGRAM_NAME "uname"

static void print_error(void) {
    fputs("try " PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("Usage: " PROGRAM_NAME " [OPTION]...\n \
            Print system information. With no OPTION, same as -s.\n\n \
            -a        print all information in the following order\n \
            -s        print the kernel name\n \
            -n        print the network hostname\n \
            -r        print the kernel release\n \
            -v        print the kernel version\n \
            -m        print the machine hardware name\n \
            -h        display this help and exit\n");
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
    bool print_sysname = false, print_nodename = false, print_release = false,
         print_version = false, print_machine = false;

    int c;
    while ((c = getopt(argc, argv, "ahmnrsv")) != -1) {
        switch (c) {
            case 'a':
                print_sysname = print_nodename = print_release = print_version = print_machine = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case 's':
                print_sysname = true;
                break;
            case 'n':
                print_nodename = true;
                break;
            case 'r':
                print_release = true;
                break;
            case 'v':
                print_version = true;
                break;
            case 'm':
                print_machine = true;
                break;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        print_error();
        return EXIT_FAILURE;
    }

    if (!print_sysname && !print_nodename && !print_release && !print_version && !print_machine) {
        print_sysname = true;
    }

    struct utsname uts;
    if (utsname(&uts) < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    if (print_sysname) {
        print_info(uts.sysname);
    }
    if (print_nodename) {
        print_info(uts.nodename);
    }
    if (print_release) {
        print_info(uts.release);
    }
    if (print_version) {
        print_info(uts.version);
    }
    if (print_machine) {
        print_info(uts.machine);
    }

    putchar('\n');
    return EXIT_SUCCESS;
}
