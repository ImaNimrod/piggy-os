#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME "date"

#define STANDARD_FORMAT "%a %b %e %H:%M:%S %Y"
#define RFC5322_FORMAT "%a, %d %b %Y %H:%M:%S"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [+FORMAT]\n   or: " PROGRAM_NAME " [MMDDhhmm[.ss]]\n\nPrint the time and date in the given FORMAT or set the date and time with [MMDDhhmm[.ss]].\n\n-R\toutput date and time in RFC 5322 format\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static inline bool is_leap_year(int year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static time_t days_per_month(int month, int year) {
    switch (month) {
        case 0: return 31;
        case 1: return is_leap_year(year) ? 29 : 28;
        case 2: return 31;
        case 3: return 30;
        case 4: return 31;
        case 5: return 30;
        case 6: return 31;
        case 7: return 31;
        case 8: return 30;
        case 9: return 31;
        case 10: return 30;
        case 11: return 31;
    }
    __builtin_unreachable();
}

// TODO: support year/century when setting system datetime
static bool parse_time(char* time_string, struct tm* tm) {
    char* seconds = NULL;

    size_t seconds_index = strcspn(time_string, ".");
    if (seconds_index != strlen(time_string)) {
        seconds = time_string + seconds_index + 1;
        time_string[seconds_index] = '\0';
    }

    if (strlen(time_string) != 8) {
        return false;
    }

    if (seconds && strlen(seconds) != 2) {
        return false;
    }

    char* c = time_string;
    while (*c != '\0') {
        if (!isdigit(*c)) {
            return false;
        }
        c++;
    }

    if (seconds) {
        c = seconds;
        while (*c != '\0') {
            if (!isdigit(*c)) {
                return false;
            }
            c++;
        }
    }

    time_t now = time(NULL);
    struct tm* now_tm = localtime(&now);

    char buf[3];
    size_t i = 0;

    strncpy(buf, time_string + i, sizeof(buf) - 1);
    i += sizeof(buf) - 1;
    int monthi = atoi(buf) - 1;
    if (monthi > 11) {
        return false;
    }
    strncpy(buf, time_string + i, sizeof(buf) - 1);
    i += sizeof(buf) - 1;
    int dayi = atoi(buf);
    if (dayi > days_per_month(monthi, now_tm->tm_year)) {
        return false;
    }
    strncpy(buf, time_string + i, sizeof(buf) - 1);
    i += sizeof(buf) - 1;
    int houri = atoi(buf);
    if (houri > 23) {
        return false;
    }
    strncpy(buf, time_string + i, sizeof(buf) - 1);
    i += sizeof(buf) - 1;
    int minutei = atoi(buf);
    if (minutei > 59) {
        return false;
    }

    int secondi = 0;
    if (seconds) {
        strncpy(buf, seconds, sizeof(buf) - 1);
        i += sizeof(buf) - 1;
        secondi = atoi(buf);
        if (secondi > 59) {
            return false;
        }
    }

    *tm = (struct tm) {
        .tm_sec = seconds ? secondi : now_tm->tm_sec,
        .tm_min = minutei,
        .tm_hour = houri,
        .tm_mon = monthi,
        .tm_year = now_tm->tm_year,
        .tm_mday = dayi,
        .tm_yday = now_tm->tm_yday,
        .tm_isdst = now_tm->tm_isdst
    };

    return true;
}

int main(int argc, char** argv) {
    bool rfc5322 = false;

    int c;
    while ((c = getopt(argc, argv, "hR")) != -1) {
        switch (c) {
            case 'R':
                rfc5322 = true;
                break;
            case 'h':
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    if (optind + 1 < argc) {
        fprintf(stderr, PROGRAM_NAME ": extra operand '%s'\n", argv[optind + 1]);
        error();
    }

    const char* format = STANDARD_FORMAT;
    char* set_time = NULL;

    if (optind < argc) {
        if (*argv[optind] == '+') {
            if (rfc5322) {
                fputs(PROGRAM_NAME ": multiple output formats specified\n", stderr);
                return EXIT_FAILURE;
            }
            format = argv[optind];
        } else {
            set_time = argv[optind];
        }
    } else if (rfc5322) {
        format = RFC5322_FORMAT;
    }

    struct tm* tm;
    if (set_time) {
        struct tm set_tm;
        if (!parse_time(set_time, &set_tm)) {
            fprintf(stderr, PROGRAM_NAME ": invalid date '%s'\n", set_time);
            return EXIT_FAILURE;
        }

        const struct timespec tp = {
            .tv_sec = mktime(&set_tm),
            .tv_nsec = 0
        };

        if (clock_settime(CLOCK_REALTIME, &tp) < 0) {
            fprintf(stderr, PROGRAM_NAME ": cannot set date: %s\n", strerror(errno));
        }
        tm = &set_tm;
    } else {
        time_t now = time(NULL);
        tm = localtime(&now);
    }

    char buffer[64];

    if (strftime(buffer, sizeof(buffer), format, tm) == 0) {
        fputs(PROGRAM_NAME ": unable to create formatted timestamp\n", stderr);
        return EXIT_FAILURE;
    } 

    puts(buffer);
    putchar('\n');
    return EXIT_SUCCESS;
}
