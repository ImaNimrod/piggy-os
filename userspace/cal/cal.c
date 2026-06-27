#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    MODE_DEFAULT,
    MODE_YEAR,
    MODE_ROLLING_YEAR,
};

static const int days_in_months[12] = {
    31, 28, 31, 30, 31, 30, 31,
    31, 30, 31, 30, 31,
};

static inline bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static inline int days_in_month(int month, int year) {
    if (month == 1) {
        return is_leap(year + 1900) ? 29 : 28;
    }

    return days_in_months[month];
}

static inline void prev_month(struct tm* t) {
    t->tm_mon--;
    if (mktime(t) == (time_t) -1) {
        errx(EXIT_FAILURE, "mktime");
    }
}

static inline void next_month(struct tm* t) {
    t->tm_mon++;
    if (mktime(t) == (time_t) -1) {
        errx(EXIT_FAILURE, "mktime");
    }
}

static inline void spaces(size_t n) {
    for (size_t i = 0; i < n; i++) {
        putchar(' ');
    }
}

static void print_calendars(const struct tm* today, struct tm* target, int count, bool is_year) {
    time_t target_time;

    struct tm actual[count];
    struct tm* timeinfo[count];

    for (int i = 0; i < count; i++) {
        target_time = mktime(target);
        if (!localtime_r(&target_time, &actual[i])) {
            err(EXIT_FAILURE, "localtime_r");
        }

        timeinfo[i] = &actual[i];
        next_month(target);
    }

    for (int i = 0; i < count; i++) {
        char month[32];
        strftime(month, sizeof(month), is_year ? "%B" : "%B %Y", timeinfo[i]);

        size_t month_len = strlen(month);
        int len = (20 - (int) month_len) / 2;

        spaces(len);
        printf("%s", month);
        spaces(22 - month_len - len);
    }

    printf("\n");
    for (int i = 0; i < count; i++) {
        printf("Su Mo Tu We Th Fr Sa  ");
    }
    printf("\n");

    int dim[count];
    int mday[count];
    int wday[count];

    for (int i = 0; i < count; i++) {
        dim[i] = days_in_month(timeinfo[i]->tm_mon, timeinfo[i]->tm_year + 1900);

        mday[i] = timeinfo[i]->tm_mday;
        wday[i] = timeinfo[i]->tm_wday;

        while (mday[i] > 1) {
            mday[i]--;
            wday[i] = (wday[i] + 6) % 7;
        }
    }

    int maybe_stop = 0;

    while (maybe_stop < count) {
        for (int i = 0; i < count; ++i) {
            if (mday[i] == 1) {
                spaces(wday[i] * 3);
            }

            bool printed = false;

            while (mday[i] <= dim[i]) {
                if (mday[i] == today->tm_mday && timeinfo[i]->tm_mon == today->tm_mon && timeinfo[i]->tm_year == today->tm_year) {
                    printf("\033[7m%2d\033[0m ", mday[i]);
                } else {
                    printf("%2d ", mday[i]);
                }

                mday[i]++;
                wday[i] = (wday[i] + 1) % 7;
                printed = true;

                if (wday[i] == 0) {
                    break;
                }
            }

            if (i + 1 != count) {
                if (!printed) {
                    spaces(22);
                } else {
                    for (int j = wday[i]; j != 0 && j < 7; j++) {
                        printf("   ");
                    }
                    printf(" ");
                }
            } else {
                printf("\n");
            }

            if (mday[i] > dim[i]) {
                maybe_stop++;
            }
        }
	}
}

static void usage(void) {
    fprintf(stderr, "usage: cal [-3Yy] [-n NUMBER]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int mode = MODE_DEFAULT;
    int month_count = 1;

    int opt;

    char* end_ptr;

    while ((opt = getopt(argc, argv, "3n:yY")) != -1) {
        switch (opt) {
            case '3':
                month_count = 3;
                break;
            case 'n':
                errno = 0;
                month_count = strtol(optarg, &end_ptr, 10);
                if (errno != 0 || month_count <= 0 || optarg == end_ptr) {
                    warnx("invalid month count: '%s'", optarg);
                    usage();
                }
                break;
            case 'y':
                month_count = 12;
                mode = MODE_YEAR;
                break;
            case 'Y':
                month_count = 12;
                mode = MODE_ROLLING_YEAR;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 2) {
        warnx("extra operands provided");
        usage();
    }

    time_t now = time(NULL);

    struct tm today;
    localtime_r(&now, &today);

    struct tm target = {};
    target.tm_year = today.tm_year;
    target.tm_mon = today.tm_mon;
    target.tm_mday = 3;

    bool explicit_date = false;

    if (argc == 1) {
        char* end1;

        errno = 0;
        long year = strtol(argv[0], &end1, 10);
        if (errno != 0 || year <= 0 || argv[0] == end1) {
            warnx("invalid year: '%s'", argv[0]);
            usage();
        }

        target.tm_year = year - 1900;
        target.tm_mon = 0;
        month_count = 12;

        explicit_date = true;
        mode = MODE_YEAR;
    } else if (argc == 2) {
        char* end1;
        char* end2;

        errno = 0;
        long month = strtol(argv[0], &end1, 10);
        if (errno != 0 || month <= 0 || month > 12 || argv[0] == end1) {
            warnx("invalid month: '%s'", argv[0]);
            usage();
        }

        errno = 0;
        long year = strtol(argv[1], &end2, 10);
        if (errno != 0 || year <= 0 || argv[1] == end2) {
            warnx("invalid year: '%s'", argv[1]);
            usage();
        }

        target.tm_mon = month - 1;
        target.tm_year = year - 1900;
    }

    if (mode == MODE_YEAR && !explicit_date) {
        target.tm_mon = 0;
    }

    if (mktime(&target) == (time_t) -1) {
        errx(EXIT_FAILURE, "mktime");
    }

    if (!explicit_date && month_count == 3 && mode == MODE_DEFAULT) {
        prev_month(&target);
    }

    int remaining = month_count;
    struct tm row_start = target;

    if (mode == MODE_YEAR) {
        spaces(30);
        printf("%d", target.tm_year + 1900);
        spaces(30);
        printf("\n\n");
    }

    while (remaining > 0) {
        int chunk = MIN(remaining, 3);

        print_calendars(&today, &row_start, chunk, mode == MODE_YEAR);

        remaining -= chunk;
        if (remaining > 0) {
            printf("\n");
        }
    }

    return EXIT_SUCCESS;
}
