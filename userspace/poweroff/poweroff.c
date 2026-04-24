#include <piggy/poweroff.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void);

static void multiple_actions(void) {
    warnx("multiple poweroff actions specified");
    usage();
}

static void usage(void) {
    fprintf(stderr, "usage: poweroff -hrs\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int action = 0;

    int c;
    while ((c = getopt(argc, argv, "hrs")) != -1) {
        switch (c) {
            case 'h':
                if (action != 0) {
                    multiple_actions();
                }
                action = POWEROFF_HALT;
                break;
            case 'r':
                if (action != 0) {
                    multiple_actions();
                }
                action = POWEROFF_REBOOT;
                break;
            case 's':
                if (action != 0) {
                    multiple_actions();
                }
                action = POWEROFF_SHUTDOWN;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 0) {
        warnx("extra operands provided");
        usage();
    }

    if (action == 0) {
        warnx("poweroff action not provided");
        usage();
    }

    int ret = poweroff(action);
    if (ret < 0) {
        err(EXIT_FAILURE, "poweroff");
    }

    return EXIT_SUCCESS;
}
