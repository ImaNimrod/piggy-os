#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static inline time_t seconds(time_t time) {
    return time % 60ULL;
}

static inline time_t minutes(time_t time) {
    return (time / 60ULL) % 60ULL;
}

static inline time_t hours(time_t time) {
    return (time / (60ULL * 60ULL)) % 24ULL;
}

static inline time_t hours_uncapped(time_t time) {
    return (time / (60ULL * 60ULL));
}

static inline time_t days(time_t time) {
    return time / (60ULL * 60ULL * 24ULL);
}

static void pretty_print_element(time_t time, const char* single, const char* multiple) {
    static const char* pretty_print_prefix = "";

    if (time == 0) {
        return;
    }

    printf("%s%jd %s", pretty_print_prefix, (intmax_t) time, 2 <= time ? multiple : single);
    pretty_print_prefix = ", ";
}

static void usage(void) {
    fprintf(stderr, "usage: uptime [-pr]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool print_pretty = false;
    bool print_raw = false;

    int c;
    while ((c = getopt(argc, argv, "pr")) != -1) {
        switch (c) {
            case 'p':
                print_pretty = true;
                break;
            case 'r':
                print_raw = true;
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

    if (print_pretty && print_raw) {
        errx(EXIT_FAILURE, "multiple print formats specified");
    }

    struct timespec realtime;
    if (clock_gettime(CLOCK_REALTIME, &realtime) < 0) {
        err(EXIT_FAILURE, "clock_realtime(CLOCK_REALTIME)");
    }

    struct timespec uptime;
    if (clock_gettime(CLOCK_BOOTTIME, &uptime) < 0) {
        err(EXIT_FAILURE, "clock_gettime(CLOCK_BOOTTIME)");
    }

    if (print_pretty) {
        printf("up ");
        pretty_print_element(days(uptime.tv_sec), "day", "days");
        pretty_print_element(hours(uptime.tv_sec), "hour", "hours");
        pretty_print_element(minutes(uptime.tv_sec), "min", "mins");
        pretty_print_element(seconds(uptime.tv_sec), "sec", "secs");
        putchar('\n');
    } else if (print_raw) {
        printf("%jd %jd.%09ld\n",
                (intmax_t) realtime.tv_sec, (intmax_t) uptime.tv_sec, uptime.tv_nsec);
    } else {
        struct tm realtime_tm;
        if (localtime_r(&realtime.tv_sec, &realtime_tm) == NULL) {
            err(EXIT_FAILURE, "localtime_r");
        }

        char realtime_buf[64];
        strftime(realtime_buf, sizeof(realtime_buf), "%H:%M:%S", &realtime_tm);

        printf("%s up %jd:%02jd\n",
                realtime_buf, (intmax_t) hours_uncapped(uptime.tv_sec), (intmax_t) minutes(uptime.tv_sec));
    }

    return EXIT_SUCCESS;
}
