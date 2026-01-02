#include <err.h>
#include <piggy/poweroff.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: poweroff -hrs\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int how = 0;

    int c;
    while ((c = getopt(argc, argv, "hrs")) != -1) {
        switch (c) {
            case 'h':
                if (how != 0) {
                    warnx("error: multiple poweroff actions specified");
                    usage();
                }

                how = POWEROFF_HALT;
                break;
            case 'r':
                if (how != 0) {
                    warnx("error: multiple poweroff actions specified");
                    usage();
                }

                how = POWEROFF_REBOOT;
                break;
            case 's':
                if (how != 0) {
                    warnx("error: multiple poweroff actions specified");
                    usage();
                }

                how = POWEROFF_SHUTDOWN;
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 0) {
        warnx("error: extra operands provided");
        usage();
    }

    if (how == 0) {
        warnx("error: poweroff action not provided");
        usage();
    }

    int ret = poweroff(how);
    if (ret < 0) {
        err(EXIT_FAILURE, "poweroff");
    }

    return EXIT_SUCCESS;
}
