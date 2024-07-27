#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

static void print_info(char* str) {
    static bool first_time = true;
    if (!first_time) {
        putchar(' ');
    }

    puts(str);
    first_time = false;
}

// TODO: print nice error messages once printf functions are implemented
int main(int argc, char* const argv[]) {
    bool print_sysname = false, print_nodename = false, print_release = false,
         print_version = false, print_machine = false;

    int c;
    while ((c = getopt(argc, argv, "amnrsv")) != -1) {
        switch (c) {
            case 'a':
                print_sysname = print_nodename = print_release = print_version = print_machine = true;
                break;
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
                return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        return EXIT_FAILURE;
    }

    if (!print_sysname && !print_nodename && !print_release && !print_version && !print_machine) {
        print_sysname = true;
    }

    struct utsname uts;
    utsname(&uts);

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
