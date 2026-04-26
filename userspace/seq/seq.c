#include <ctype.h>
#include <err.h> 
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ISEXP(c)	((int) (c) == 'e' || (int) (c) == 'E')
#define ISODIGIT(c)	((int) (c) >= '0' && (int) (c) <= '7')
#define ISSIGN(c)	((int) (c) == '-' || (int) (c) == '+')

#define MAX(a, b) (((a) < (b)) ? (b) : (a))

static char* decimal_point = ".";

static double checked_strtod(const char* s) {
    errno = 0;

    char* end_ptr;
    double ret = strtod(s, &end_ptr);

    if (errno == ERANGE) {
        err(EXIT_FAILURE, "%s", s);
    } else if (*end_ptr != '\0') {
        errx(EXIT_FAILURE, "invalid floating point argument: '%s'", s);
    }

    if (ret == -0.0) {
        ret = 0.0;
    }

    return ret;
}

static int decimal_places(const char* s) {
    const char* dot = strchr(s, '.');
    if (dot == NULL) {
        return 0;
    }

    int n = 0;
    for (const char* p = dot + 1; isdigit((unsigned char) *p); p++) {
        n++;
    }

    return n;
}

static char* generate_format(double start, double increment, double end) {
    static char buf[256];

    char cc = '\0';

    if (start > end) {
        end = start - increment * floor((start - end) / increment);
    } else {
        end = start + increment * floor((end - start) / increment);
    }

    sprintf(buf, "%g", increment);
    if (strchr(buf, 'e')) {
        cc = 'e';
    }

    int precision = decimal_places(buf);

    int width1 = sprintf(buf, "%g", start);
    if (strchr(buf, 'e')) {
        cc = 'e';
    }

    int places;
    if ((places = decimal_places(buf))) {
        width1 -= (places + strlen(decimal_point));
    }

    precision = MAX(places, precision);

    int width2 = sprintf(buf, "%g", end);
    if (strchr(buf, 'e')) {
        cc = 'e';
    }
    if ((places = decimal_places(buf))) {
        width2 -= (places + strlen(decimal_point));
    }

    if (precision) {
        sprintf(buf, "%%%d.%d%c",
                MAX(width1, width2) + (int) strlen(decimal_point) +
                precision, precision, (cc) ? cc : 'f');
    } else {
        sprintf(buf, "%%%d%c", MAX(width1, width2), (cc) ? cc : 'g');
    }

    return buf;
}

static bool is_numeric(char* s) {
    if (ISSIGN((unsigned char) *s)) {
        s++;
    }

    size_t seen_decimal_pt = 0;
    size_t decimal_pt_len = strlen(decimal_point);
    while (*s != '\0') {
        if (!isdigit((unsigned char) *s)) {
            if (!seen_decimal_pt && strncmp(s, decimal_point, decimal_pt_len) == 0) {
                s += decimal_pt_len;
                seen_decimal_pt = 1;
                continue;
            }

            if (ISEXP((unsigned char) *s)) {
                s++;
                if (ISSIGN((unsigned char) *s) || isdigit((unsigned char) *s)) {
                    s++;
                    continue;
                }
            }

            break;
        }

        s++;
    }

    return *s == '\0';
}

static char* sanitize(char* s) {
    char* src = (char*) s;
    char* dst = (char*) s;

    while (*src != '\0') {
        if (*src != '\\') {
            *dst++ = *src++;
            continue;
        }

        src++;

        int value = 0;
        int digits = 0;

        switch (*src) {
            case 'a': *dst++ = '\a'; src++; break;
            case 'b': *dst++ = '\b'; src++; break;
            case 'e': *dst++ = '\033'; src++; break;
            case 'f': *dst++ = '\f'; src++; break;
            case 'n': *dst++ = '\n'; src++; break;
            case 'r': *dst++ = '\r'; src++; break;
            case 't': *dst++ = '\t'; src++; break;
            case 'v': *dst++ = '\v'; src++; break;
            case '\\': *dst++ = '\\'; src++; break;
            case '\'': *dst++ = '\''; src++; break;
            case '"': *dst++ = '"';  src++; break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                while (digits < 3 && *src >= '0' && *src <= '7') {
                  value = (value << 3) | (*src - '0');
                  src++;
                  digits++;
                }

                *dst++ = (char) value;
                break;
            case 'x':
                src++;

                while (digits < 2 && isxdigit((unsigned char) *src)) {
                  value <<= 4;

                  if (isdigit((unsigned char) *src)) {
                      value |= *src - '0';
                  } else {
                      value |= (tolower((unsigned char) *src) - 'a' + 10);
                  }

                  src++;
                  digits++;
                }

                *dst++ = (char) value;
                break;
            default:
                *dst++ = '\\';
                break;
        }
    }

    *dst = '\0';
    return s;
}

static void usage(void) {
    fprintf(stderr, "usage: seq [-s SEPERATOR] [FIRST [INCREMENT]] LAST\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char* separator = "\n";

    struct lconv* locale = localeconv();
    if (locale != NULL && locale->decimal_point != NULL && locale->decimal_point[0] != '\0') {
        decimal_point = locale->decimal_point;
    }

    int c;
    while ((c = getopt(argc, argv, "s:")) != -1 && !is_numeric(argv[optind])) {
        switch (c) {
            case 's':
                separator = sanitize(optarg);
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    } else if (argc > 3) {
        warnx("extra operands provided");
        usage();
    }

    double start = 1.0;
    double increment = 1.0;
    double end;

    if (argc >= 2) {
        start = checked_strtod(argv[0]);
    }

    if (argc == 3) {
        increment = checked_strtod(argv[1]);
        if (increment == 0.0) {
            warn("invalid zero increment value");
            usage();
        }
    }

    errno = 0;
    end = checked_strtod(argv[argc - 1]);

    if ((end < start && increment > 0) || (end > start && increment < 0)) {
        return EXIT_SUCCESS;
    }

    const char* fmt = generate_format(start, increment, end);
    long long count = (long long) ((end - start) / increment + 1e-12);

    printf(fmt, start);
    for (long long i = 1; i <= count; i++) {
        fputs(separator, stdout);
        printf(fmt, start + (i * increment));
    }

    putchar('\n');
    return EXIT_SUCCESS;
}
