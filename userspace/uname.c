#include <sys/utsname.h>

#include <err.h> 
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum {
    PRINT_SYSNAME   = 0x01,
    PRINT_NODENAME  = 0x02,
    PRINT_RELEASE   = 0x04,
    PRINT_VERSION   = 0x08,
    PRINT_MACHINE   = 0x10,
};

static void print_info(char* str) {
    static bool space = false;
    if (space) {
        putchar(' ');
    }

    printf("%s", str);
    space = true;
}

static void usage(void) {
    fprintf(stderr, "usage: uname [-amnrsv]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int print_mode = PRINT_SYSNAME;

    int c;
    while ((c = getopt(argc, argv, "amnrsv")) != -1) {
        switch (c) {
            case 'a':
                print_mode |= (PRINT_SYSNAME | PRINT_NODENAME | PRINT_RELEASE | PRINT_VERSION | PRINT_MACHINE);
                break;
            case 'm':
                print_mode |= PRINT_MACHINE;
                break;
            case 'n':
                print_mode |= PRINT_NODENAME;
                break;
            case 'r':
                print_mode |= PRINT_RELEASE;
                break;
            case 's':
                print_mode |= PRINT_SYSNAME;
                break;
            case 'v':
                print_mode |= PRINT_VERSION;
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 0) {
        warnx("extra operands provided");
        usage();
    }

    struct utsname uts;
    if (uname(&uts) < 0) {
        err(EXIT_FAILURE, "uname");
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
