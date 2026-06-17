#include <piggy/hwclock.h>

#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> 

#define _PATH_DEVRTC "/dev/rtc"

enum {
    PRINT_TIME,
    HW_TO_SYS,
    SYS_TO_HW,
};

static void usage(void);

static int hardware_to_system(void) {
    int rtc = open(_PATH_DEVRTC, O_RDONLY);
    if (rtc < 0) {
        warn("open(%s)", _PATH_DEVRTC);
        return EXIT_FAILURE;
    }

    struct rtc_time rtc_time;

    int ret = ioctl(rtc, HWCLOCK_GETTIME, &rtc_time);
    if (ret < 0) {
        warn("ioctl");
        close(rtc);
        return EXIT_FAILURE;
    }

    close(rtc);
    
    struct tm tm_time = {};
    tm_time.tm_sec = rtc_time.second;
    tm_time.tm_min = rtc_time.minute;
    tm_time.tm_hour = rtc_time.hour;
    tm_time.tm_mday = rtc_time.day;
    tm_time.tm_mon  = rtc_time.month - 1;
    tm_time.tm_year = rtc_time.year - 1900;

    time_t epoch = mktime(&tm_time);
    if (epoch == (time_t) -1) {
        warnx("mktime");
        return EXIT_FAILURE;
    }

    struct timespec ts = { epoch, 0 };

    if (clock_settime(CLOCK_REALTIME, &ts) == -1) {
        warn("clock_settime(CLOCK_REALTIME)");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void multiple_actions(void) {
    warnx("multiple poweroff actions specified");
    usage();
}

static int print_hardware_timestamp(void) {
    int rtc = open(_PATH_DEVRTC, O_RDONLY);
    if (rtc < 0) {
        warn("open(%s)", _PATH_DEVRTC);
        return EXIT_FAILURE;
    }

    struct rtc_time rtc_time;

    int ret = ioctl(rtc, HWCLOCK_GETTIME, &rtc_time);
    if (ret < 0) {
        warn("ioctl");
        close(rtc);
        return EXIT_FAILURE;
    }

    close(rtc);

    struct tm tm_time = {};
    tm_time.tm_sec = rtc_time.second;
    tm_time.tm_min = rtc_time.minute;
    tm_time.tm_hour = rtc_time.hour;
    tm_time.tm_mday = rtc_time.day;
    tm_time.tm_mon  = rtc_time.month - 1;
    tm_time.tm_year = rtc_time.year - 1900;

    char buf[128];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_time) == 0) {
        warnx("strftime");
        return EXIT_FAILURE;
    }

    puts(buf);
    return EXIT_SUCCESS;
}

static int system_to_hardware(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        warn("clock_gettime(CLOCK_REALTIME)");
        return EXIT_FAILURE;
    }

    struct tm* tm_time = gmtime(&ts.tv_sec);

    struct rtc_time rtc_time = {
        .second = tm_time->tm_sec,
        .minute = tm_time->tm_min,
        .hour = tm_time->tm_hour,
        .day = tm_time->tm_mday,
        .month = tm_time->tm_mon + 1,
        .year = tm_time->tm_year + 1900,
    };

    int rtc = open(_PATH_DEVRTC, O_RDONLY);
    if (rtc < 0) {
        warn("open");
        return EXIT_FAILURE;
    }

    int ret = ioctl(rtc, HWCLOCK_SETTIME, &rtc_time);
    if (ret < 0) {
        warn("ioctl");
        close(rtc);
        return EXIT_FAILURE;
    }

    close(rtc);
    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: hwclock [-hs]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int action = PRINT_TIME;

    int c;
    while ((c = getopt(argc, argv, "hs")) != -1) {
        switch (c) {
            case 'h':
                if (action != PRINT_TIME) {
                    multiple_actions();
                }
                action = HW_TO_SYS;
                break;
            case 's':
                if (action != PRINT_TIME) {
                    multiple_actions();
                }
                action = SYS_TO_HW;
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

    int ret = EXIT_SUCCESS;

    switch (action) {
        case PRINT_TIME:
            ret = print_hardware_timestamp();
            break;
        case HW_TO_SYS:
            ret = hardware_to_system();
            break;
        case SYS_TO_HW:
            ret = system_to_hardware();
            break;
    }

    return ret;
}
