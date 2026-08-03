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
                [[fallthrough]];
            case 'h':
                [[fallthrough]];
            case 'm':
                num *= 60;
                [[fallthrough]];
            case 's':
                if (!isnan(num)) {
                    return (num);
                }
        }
    }

    errx(EXIT_FAILURE, "invalid time interval '%s'", arg);
}

static void usage(void) {
    fprintf(stderr, "usage: sleep NUMBER[UNIT]...\n");
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

    if (seconds > LONG_MAX) {
        usage();
    }

    if (seconds < 1e-9) {
        return EXIT_SUCCESS;
    }

    struct timespec ts = {
        .tv_sec = seconds,
        .tv_nsec = 1e9 * (seconds - ((time_t) seconds)),
    };

    return (nanosleep(&ts, NULL) < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
