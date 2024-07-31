#include <stdio.h>
#include "time_internal.h"

static const char* abvdays[] = {
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat"
};

static const char* days[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
};

static const char* abvmonths[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

static const char* months[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

size_t strftime(char* buffer, size_t size, const char* fmt, const struct tm* tm) {
    char* b = buffer;

    for (const char* f = fmt; *f; f++) {
        if (*f != '%') {
            if (size == 0) {
                return 0;
            }
            size--;
            *b++ = *f;
            continue;
        }

        f++;

        int w = 0;
        switch (*f) {
            case 'a':
                w = snprintf(b, size, "%s", abvdays[tm->tm_wday]);
                break;
            case 'A':
                w = snprintf(b, size, "%s", days[tm->tm_wday]);
                break;
            case 'h':
            case 'b':
                w = snprintf(b, size, "%s", abvmonths[tm->tm_mon]);
                break;
            case 'B':
                w = snprintf(b, size, "%s", months[tm->tm_mon]);
                break;
            case 'c':
                w = snprintf(b, size, "%s %s %02d %02d:%02d:%02d %04d",
                        abvdays[tm->tm_wday],
                        abvmonths[tm->tm_mon],
                        tm->tm_mday,
                        tm->tm_hour,
                        tm->tm_min,
                        tm->tm_sec,
                        tm->tm_year + 1900);
                break;
            case 'C':
                w = snprintf(b, size, "%02d", (tm->tm_year + 1900) / 100);
                break;
            case 'd':
                w = snprintf(b, size, "%02d", tm->tm_mday);
                break;
            case 'D':
                w = snprintf(b, size, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
                break;
            case 'e':
                w = snprintf(b, size, "%2d", tm->tm_mday);
                break;
            case 'F':
                w = snprintf(b, size, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday);
                break;
            case 'H':
                w = snprintf(b, size, "%02d", tm->tm_hour);
                break;
            case 'I':
                w = snprintf(b, size, "%02d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
                break;
            case 'j':
                w = snprintf(b, size, "%03d", tm->tm_yday);
                break;
            case 'k':
                w = snprintf(b, size, "%2d", tm->tm_hour);
                break;
            case 'l':
                w = snprintf(b, size, "%2d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
                break;
            case 'm':
                w = snprintf(b, size, "%02d", tm->tm_mon+1);
                break;
            case 'M':
                w = snprintf(b, size, "%02d", tm->tm_min);
                break;
            case 'n':
                w = snprintf(b, size, "\n");
                break;
            case 'p':
                w = snprintf(b, size, "%s", tm->tm_hour < 12 ? "AM" : "PM");
                break;
            case 'P':
                w = snprintf(b, size, "%s", tm->tm_hour < 12 ? "am" : "pm");
                break;
            case 'r':
                w = snprintf(b, size, "%02d:%02d:%02d %s",
                        tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)),
                        tm->tm_min,
                        tm->tm_sec,
                        tm->tm_hour < 12 ? "AM" : "PM");
                break;
            case 'R':
                w = snprintf(b, size, "%02d:%02d", tm->tm_hour, tm->tm_min);
                break;
            case 's':
                w = snprintf(b, size, "%ld", mktime((struct tm*)tm));
                break;
            case 'S':
                w = snprintf(b, size, "%02d", tm->tm_sec);
                break;
            case 't':
                w = snprintf(b, size, "\t");
                break;
            case 'T':
                w = snprintf(b, size, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
                break;
            case 'u':
                w = snprintf(b, size, "%d", tm->tm_wday == 0 ? 7 : tm->tm_wday);
                break;
            case 'w':
                w = snprintf(b, size, "%d", tm->tm_wday);
                break;
            case 'x':
                w = snprintf(b, size, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
                break;
            case 'X':
                w = snprintf(b, size, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
                break;
            case 'y':
                w = snprintf(b, size, "%02d", tm->tm_year % 100);
                break;
            case 'Y':
                w = snprintf(b, size, "%04d", tm->tm_year + 1900);
                break;
            case '%':
                w = snprintf(b, size, "%c", '%');
                break;
            case 'z':
            case 'Z':
            case 'V':
            case 'W':
            case 'U':
            case 'G':
            case 'g':
                w = snprintf(b, size, "%c unsupported", *f);
                break;
        }

        if (w < 0 || (size_t) w >= size) {
            return 0;
        }

        size -= w;
        b += w;
    }

    *b = '\0';
    return b - buffer;
}
