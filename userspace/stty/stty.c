#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define CTRL(c) ((c) & 0x1f)
#define SIZEOF_ARRAY(xs) (sizeof((xs)) / sizeof((xs)[0]))

struct terminal_flag {
    const char* name;
    union {
        tcflag_t bit;
        cc_t character;
    } ;
};

static const struct terminal_flag iflags[] = {
    { "brkint", .bit = BRKINT },
    { "icrnl",  .bit = ICRNL },
    { "ignbrk", .bit = IGNBRK },
    { "igncr",  .bit = IGNCR },
    { "ignpar", .bit = IGNPAR },
    { "inlcr",  .bit = INLCR },
    { "inpck",  .bit = INPCK },
    { "istrip", .bit = ISTRIP },
    { "ixany",  .bit = IXANY },
    { "ixoff",  .bit = IXOFF },
    { "ixon",   .bit = IXON },
    { "parmrk", .bit = PARMRK },
};

static const struct terminal_flag oflags[] = {
    { "opost",  .bit = OPOST },
    { "onlcr",  .bit = ONLCR },
    { "ocrnl",  .bit = OCRNL },
    { "onocr",  .bit = ONOCR },
    { "onlret", .bit = ONLRET },
    { "ofill",  .bit = OFILL },
    { "ofdel",  .bit = OFDEL },
};

static const struct terminal_flag cflags[] = {
    { "clocal", .bit = CLOCAL },
    { "cread",  .bit = CREAD },
    { "csize",  .bit = CSIZE },
    { "cstop",  .bit = CSTOPB },
    { "hupcl",  .bit = HUPCL },
    { "parenb", .bit = PARENB },
    { "parodd", .bit = PARODD },
};

static const struct terminal_flag lflags[] = {
    { "echo",   .bit = ECHO },
    { "echoe",  .bit = ECHOE },
    { "echok",  .bit = ECHOK },
    { "echonl", .bit = ECHONL },
    { "icanon", .bit = ICANON },
    { "iexten", .bit = IEXTEN },
    { "isig",   .bit = ISIG },
    { "noflsh", .bit = NOFLSH },
    { "tostop", .bit = TOSTOP },
};

static const struct terminal_flag control_characters[] = {
    { "eof",    .character = VEOF },
    { "eol",    .character = VEOL },
    { "erase",  .character = VERASE },
    { "intr",   .character = VINTR },
    { "kill",   .character = VKILL },
    { "quit",   .character = VQUIT },
    { "start",  .character = VSTART },
    { "stop",   .character = VSTOP },
    { "susp",   .character = VSUSP },
    { "min",    .character = VMIN },
    { "time",   .character = VTIME },
};

static bool print_all = false;

static const struct termios sane_termios = {
    .c_iflag = BRKINT | ICRNL | IXANY | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = B38400 | CREAD | CS8,
    .c_lflag = ECHO | ECHOE | ECHOK | ECHONL | ICANON | IEXTEN | ISIG,
    .c_cc[VEOF] = CTRL('D'),
    .c_cc[VEOL] = '\0',
    .c_cc[VERASE] = CTRL('?'),
    .c_cc[VINTR] = CTRL('C'),
    .c_cc[VKILL] = CTRL('U'),
    .c_cc[VMIN] = 1,
    .c_cc[VQUIT] = CTRL('\\'),
    .c_cc[VSTART] = CTRL('Q'),
    .c_cc[VSTOP] = CTRL('S'),
    .c_cc[VSUSP] = CTRL('Z'),
    .c_cc[VTIME] = 0,
};

static const char* get_speed_name(speed_t speed) {
    switch (speed) {
        case B0: return "0";
        case B50: return "50";
        case B75: return "75";
        case B110: return "110";
        case B134: return "134";
        case B150: return "150";
        case B200: return "200";
        case B300: return "300";
        case B600: return "600";
        case B1200: return "1200";
        case B1800: return "1800";
        case B2400: return "2400";
        case B4800: return "4800";
        case B9600: return "9600";
        case B19200: return "19200";
        case B38400: return "38400";
        case B57600: return "57600";
        case B115200: return "115200";
        case B230400: return "230400";
        case B460800: return "460800";
        case B500000: return "500000";
        case B576000: return "576000";
        case B921600: return "921600";
        case B1000000: return "1000000";
        case B1152000: return "1152000";
        case B1500000: return "1500000";
        case B2000000: return "2000000";
        case B2500000: return "2500000";
        case B3000000: return "3000000";
        case B3500000: return "3500000";
        case B4000000: return "4000000";
        default: return "unknown";
    }
}

static cc_t parse_mintime(const char* string) {
    if (!isdigit((unsigned char) string[0])) {
        errx(EXIT_FAILURE, "invalid mintime quantity: %s", string);
    }

    char* end_ptr;

    errno = 0;
    uintmax_t value = strtoumax(string, &end_ptr, 10);
    if (errno != 0 || end_ptr == string || *end_ptr || value != (cc_t) value) {
        errx(EXIT_FAILURE, "invalid mintime quantity: %s", string);
    }

	return (cc_t) value;
}

static unsigned short parse_winsize(const char* string) {
    if (!isdigit((unsigned char) string[0])) {
        errx(EXIT_FAILURE, "invalid window size: %s", string);
    }

    char* end_ptr;

    errno = 0;
    uintmax_t value = strtoumax(string, &end_ptr, 10);
    if (errno != 0 || end_ptr == string || *end_ptr || value != (unsigned short) value) {
        errx(EXIT_FAILURE, "invalid window size: %s", string);
    }

    return (unsigned short) value;
}

static void print_flags(const char* type, tcflag_t value, tcflag_t default_value, const struct terminal_flag* flags, size_t flags_len) {
    printf("%s:", type);

    tcflag_t handled = 0;

    for (size_t i = 0; i < flags_len; i++) {
        const struct terminal_flag* flag = &flags[i];

        handled |= flag->bit;

        if (!print_all && (value & flag->bit) == (default_value & flag->bit)) {
            continue;
        }

        putchar(' ');

        if (strcmp(flag->name, "csize") == 0) {
            switch (value & CSIZE) {
                case CS5:
                    fputs("cs5", stdout);
                    break;
                case CS6:
                    fputs("cs6", stdout);
                    break;
                case CS7:
                    fputs("cs7", stdout);
                    break;
                case CS8:
                    fputs("cs8", stdout);
                    break;
            }
        } else {
            if (!(value & flag->bit)) {
                putchar('-');
            }
            fputs(flag->name, stdout);
        }
    }

    if (value & ~handled) {
        printf(" %#x", value & ~handled);
    }

    putchar('\n');
}

static void usage(void) {
    fprintf(stderr, "usage: stty [-F DEVICE] [SETTING]...\n"
                    "       stty [-F DEVICE] [-a]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char* tty_filename = "stdin";
    int tty_fd = STDIN_FILENO;

    int c;
    while ((c = getopt(argc, argv, "aF:")) != -1) {
        switch (c) {
            case 'a':
                print_all = true;
                break;
            case 'F':
                tty_filename = optarg;
                if ((tty_fd = open(tty_filename, O_RDONLY)) < 0) {
                    err(EXIT_FAILURE, "%s", tty_filename);
                }
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (print_all && argc >= 1) {
        errx(EXIT_FAILURE, "cannot both change and display terminal settings");
    }

    if (!isatty(tty_fd)) {
        err(EXIT_FAILURE, "%s", tty_filename);
    }

    struct winsize winsz;
    bool have_winsize = ioctl(tty_fd, TIOCGWINSZ, &winsz) == 0;

    struct termios termios;
    if (tcgetattr(tty_fd, &termios) < 0) {
        err(EXIT_FAILURE, "tcgetattr(%s)", tty_filename);
    }

    if (argc == 0) {
        printf("speed: %s baud;", get_speed_name(termios.c_cflag & CBAUD));

        if (print_all && have_winsize) {
            printf(" %u rows; %u columns;", winsz.ws_row, winsz.ws_col);
        }
        
        fputs("\ncc:", stdout);

        for (size_t i = 0; i < SIZEOF_ARRAY(control_characters); i++) {
            const struct terminal_flag* cc = &control_characters[i];

            if (!print_all && termios.c_cc[cc->character] == sane_termios.c_cc[cc->character]) {
                continue;
            }

            printf(" %s = ", cc->name);

            unsigned char value = (unsigned char) termios.c_cc[cc->character];
            if (cc->character == VMIN || cc->character == VTIME) {
                printf("%i", value);
            } else if (value == _POSIX_VDISABLE) {
                printf("undef");
            } else if (128 <= value && (value < 160 || value == 255)) {
                printf("M-^%c", (value - 128) ^ 0x40);
            } else if (128 < value) {
                printf("M-%c", value - 128);
            } else if (value < 32 || value == 127) {
                printf("^%c", value ^ 0x40);
            } else {
                putchar(value);
            }

            putchar(';');
        }

        putchar('\n');

        print_flags("iflags", termios.c_iflag, sane_termios.c_iflag, iflags, SIZEOF_ARRAY(iflags));
        print_flags("oflags", termios.c_oflag, sane_termios.c_oflag, oflags, SIZEOF_ARRAY(oflags));
        print_flags("cflags", termios.c_cflag, sane_termios.c_cflag, cflags, SIZEOF_ARRAY(cflags));
        print_flags("lflags", termios.c_lflag, sane_termios.c_lflag, lflags, SIZEOF_ARRAY(lflags));
    }

    bool set_winsize = false;

    for (int i = 0; i < argc; i++) {
        char* arg = argv[i];

        if (!strcmp(arg, "cs5")) {
            termios.c_cflag = (termios.c_cflag & ~CSIZE) | CS5;
        } else if (!strcmp(arg, "cs6")) {
            termios.c_cflag = (termios.c_cflag & ~CSIZE) | CS6;
        } else if (!strcmp(arg, "cs7")) {
            termios.c_cflag = (termios.c_cflag & ~CSIZE) | CS7;
        } else if (!strcmp(arg, "cs8")) {
            termios.c_cflag = (termios.c_cflag & ~CSIZE) | CS8;
        } else if (!strcmp(arg, "nl")) {
            termios.c_iflag = (termios.c_iflag & ~ICRNL);
        } else if (!strcmp(arg, "-nl")) {
            termios.c_iflag = (termios.c_iflag & ~(INLCR | IGNCR)) | ICRNL;
        } else if (!strcmp(arg, "ek") ) {
            termios.c_cc[VERASE] = sane_termios.c_cc[VERASE];
            termios.c_cc[VKILL] = sane_termios.c_cc[VKILL];
        } else if (!strcmp(arg, "evenp") || !strcmp(arg, "parity")) {
            termios.c_cflag = (termios.c_cflag & ~(CSIZE | PARODD)) | PARENB | CS7;
        } else if (!strcmp(arg, "oddp")) {
            termios.c_cflag = (termios.c_cflag & ~CSIZE) | PARENB | PARODD | CS7;
        } else if (!strcmp(arg, "-parity") || !strcmp(arg, "-evenp") || !strcmp(arg, "-oddp")) {
            termios.c_cflag = (termios.c_cflag & ~(CSIZE | PARENB)) | CS8;
        } else if (!strcmp(arg, "cols") || !strcmp(arg, "columns")) {
            if (i + 1 == argc) {
                errx(EXIT_FAILURE, "missing argument to %s", arg);
            }

            winsz.ws_col = parse_winsize(argv[++i]);
            set_winsize = true;
        } else if (!strcmp(arg, "rows")) {
            if (i + 1 == argc) {
                errx(EXIT_FAILURE, "missing argument to %s", arg);
            }

            winsz.ws_row = parse_winsize(argv[++i]);
            set_winsize = true;
        } else if (!strcmp(arg, "min")) {
            if (i + 1 == argc) {
                errx(EXIT_FAILURE, "missing argument to %s", arg);
            }

            termios.c_cc[VMIN] = parse_mintime(argv[++i]);
        } else if (!strcmp(arg, "time")) {
            if (i + 1 == argc) {
                errx(EXIT_FAILURE, "missing argument to %s", arg);
            }

            termios.c_cc[VTIME] = parse_mintime(argv[++i]);
        } else if (!strcmp(arg, "raw") || !strcmp(arg, "-cooked")) {
            termios.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR | ISTRIP | IXON | PARMRK);
            termios.c_oflag &= ~OPOST;
            termios.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
            termios.c_cflag |= CS8;
            termios.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
            termios.c_cc[VMIN] = 1;
            termios.c_cc[VTIME] = 0;
        } else if (!strcmp(arg, "sane") || !strcmp(arg, "cooked") || !strcmp(arg, "-raw")) {
            termios.c_iflag = sane_termios.c_iflag;
            termios.c_oflag = sane_termios.c_oflag;
            termios.c_cflag = sane_termios.c_cflag;
            termios.c_lflag = sane_termios.c_lflag;
            memcpy(&termios.c_cc, &sane_termios.c_cc, sizeof(termios.c_cc));
        } else {
            bool negated = false;
            if (arg[0] == '-') {
                arg++;
                negated = true;
            }

            for (size_t j = 0; j < SIZEOF_ARRAY(iflags); j++) {
                if (!strcmp(arg, iflags[j].name)) {
                    termios.c_iflag = (termios.c_iflag & ~iflags[j].bit) | (negated ? 0 : iflags[j].bit);
                    goto found;
                }
            }

            for (size_t j = 0; j < SIZEOF_ARRAY(oflags); j++) {
                if (!strcmp(arg, oflags[j].name)) {
                    termios.c_oflag = (termios.c_oflag & ~oflags[j].bit) | (negated ? 0 : oflags[j].bit);
                    goto found;
                }
            }

            for (size_t j = 0; j < SIZEOF_ARRAY(cflags); j++) {
                if (!strcmp(arg, cflags[j].name)) {
                    termios.c_cflag = (termios.c_cflag & ~cflags[j].bit) | (negated ? 0 : cflags[j].bit);
                    goto found;
                }
            }

            for (size_t j = 0; j < SIZEOF_ARRAY(lflags); j++) {
                if (!strcmp(arg, lflags[j].name)) {
                    termios.c_lflag = (termios.c_lflag & ~lflags[j].bit) | (negated ? 0 : lflags[j].bit);
                    goto found;
                }
            }

            for (size_t j = 0; j < SIZEOF_ARRAY(control_characters); j++) {
                if (strcmp(arg, control_characters[j].name) != 0) {
                    continue;
                }

                if (i + 1 == argc) {
                    errx(EXIT_FAILURE, "missing argument to %s", arg);
                }

                const char* parameter = argv[++i];
                if (!parameter[0] || !parameter[1]) {
                    termios.c_cc[control_characters[j].character] = parameter[0];
                } else if (!strcmp(parameter, "undef") || !strcmp(parameter, "^-")) {
                    termios.c_cc[control_characters[j].character] = _POSIX_VDISABLE;
                } else if (parameter[0] == '^' && (('@' <= parameter[1] && parameter[1] <= '_') || ('a' <= parameter[1] && parameter[1] <= 'z') || parameter[1] == '?') && !parameter[2]) {
                    termios.c_cc[control_characters[j].character] = CTRL(parameter[1]);
                } else if (isdigit((unsigned char) parameter[0]) && isdigit((unsigned char) parameter[1]) && (!parameter[2] || (isdigit((unsigned char) parameter[2]) && !parameter[3]))) {
                    int value = atoi(parameter);
                    if (value <= 255) {
                        termios.c_cc[control_characters[j].character] = value;
                    }
                } else {
                    errx(EXIT_FAILURE, "invalid control character: %s", parameter);
                }
            }

            errx(EXIT_FAILURE, "unknown terminal setting: %s", arg);
found:
        }
    }

    if (tcsetattr(tty_fd, TCSANOW, &termios) < 0) {
        err(EXIT_FAILURE, "tcsetattr(%s)", tty_filename);
    }

    if (set_winsize && ioctl(tty_fd, TIOCSWINSZ, &winsz) < 0) {
        err(EXIT_FAILURE, "ioctl(%s, TIOCSWINSZ)", tty_filename);
    }

    return EXIT_SUCCESS;
}
