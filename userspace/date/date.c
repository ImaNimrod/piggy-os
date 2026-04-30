#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum {
    FORMAT_DEFAULT  = 0,
    FORMAT_ISO8601  = 1,
    FORMAT_RFC5332  = 2,
};

static const char* formats[] = {
    [FORMAT_DEFAULT] = "%a %b %e %T %Z %Y",
    [FORMAT_ISO8601] = "%Y-%m-%dT%TZ",
    [FORMAT_RFC5332] = "%a, %d %b %Y %T %z",
};

static void usage(void);

static void multiple_formats(void) {
    warnx("multiple formats specified");
    usage();
}

static void usage(void) {
    fprintf(stderr, "usage: date [-IRuz:] [+FORMAT]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int format = FORMAT_DEFAULT;
    char* out_zone = NULL;

    int c;
    while ((c = getopt(argc, argv, "IRuz:")) != -1) {
        switch (c) {
            case 'I':
                if (format != FORMAT_DEFAULT) {
                    multiple_formats();
                }
                format = FORMAT_ISO8601;
                break;
            case 'R':
                if (format != FORMAT_DEFAULT) {
                    multiple_formats();
                }
                format = FORMAT_RFC5332;
                break;
            case 'u':
                setenv("TZ", "UTC0", 1);
                break;
            case 'z':
                out_zone = optarg;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    setlocale(LC_TIME, "");

    const char* format_string;

    if (*argv != NULL && *argv[0] == '+') {
        if (format != FORMAT_DEFAULT) {
            multiple_formats();
        }

        format_string = *argv + 1;
    } else {
        format_string = formats[format];
    }

    if (out_zone != NULL && setenv("TZ", out_zone, 1) != 0) {
        err(EXIT_FAILURE, "setenv(TZ)");
    }

    if (format == FORMAT_RFC5332) {
        setlocale(LC_TIME, "C");
    }

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    char buffer[1024];
    if (strftime(buffer, sizeof(buffer), format_string, tm) == 0) {
        putchar('\n');
    } else {
        puts(buffer);
    }

    return EXIT_SUCCESS;
}
