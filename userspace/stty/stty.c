#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
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
    { "min",    .character = VMIN },
    { "quit",   .character = VQUIT },
    { "start",  .character = VSTART },
    { "stop",   .character = VSTOP },
    { "susp",   .character = VSUSP },
    { "time",   .character = VTIME },
};

static bool print_all = false;

static const struct termios sane_termios = {
    .c_iflag = BRKINT | ICRNL | IXANY | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CREAD | CS8,
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

    // TODO: support setting terminal arguments

    return EXIT_SUCCESS;
}
