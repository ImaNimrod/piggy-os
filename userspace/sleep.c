#include <err.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double parse_interval(const char* arg) {
    double num;
    char unit, extra;

    int count = sscanf(arg, "%lf%c%c", &num, &unit, &extra);
    if (count == 1) {
        if (!isnan(num)) {
            return num;
        }
    } else if (count == 2) {
        switch (unit) {
            case 'd':
                num *= 24;
                /* FALLTHROUGH */
            case 'h':
                num *= 60;
                /* FALLTHROUGH */
            case 'm':
                num *= 60;
                /* FALLTHROUGH */
            case 's':
                if (!isnan(num)) {
                    return (num);
                }
        }
    }

    fprintf(stderr, "invalid time interval: %s\n", arg);
    exit(EXIT_FAILURE);
}

static void usage(void) {
    fprintf(stderr, "usage: sleep NUMBER[UNIT]...\n"
            "UNIT can be 's', 'm', 'h', or 'd', for seconds, minutes, hours, or days.\n"
            "If multiple arguments are provided, pause for the sum of their values.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    double seconds = 0;
    while (argc--) {
        seconds += parse_interval(*argv++);
    }

    if (seconds > INT_MAX) {
        usage();
    }

    if (seconds < 1e-9) {
        return EXIT_SUCCESS;
    }

    struct timespec ts = {
        .tv_sec = seconds,
        .tv_nsec = 1e9 * (seconds - ((time_t) seconds)),
    };

    if (nanosleep(&ts, NULL) < 0) {
        err(EXIT_FAILURE, "nanosleep");
    }

    return EXIT_SUCCESS;
}
