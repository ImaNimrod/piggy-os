#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool parse_pid(const char* str, pid_t* ret_pid) {
    char* end_ptr;

    errno = 0;
    intmax_t pid = strtoimax(str, &end_ptr, 10);
    if (errno != 0 || end_ptr == str || *end_ptr) {
        return false;
    }

    if (pid < 0 || pid != (pid_t) pid) {
        return false;
    }

    *ret_pid = pid;
    return true;
}

static bool parse_signal(char* str, int* ret_signum) {
    if (isdigit((unsigned char) str[0])) {
        char* end_ptr;

        errno = 0;
        long l = strtol(str, &end_ptr, 10);
        if (errno != 0 || end_ptr == str || *end_ptr) {
            return false;
        }

        if (l <= 0 || l >= NSIG) {
            return false;
        }

        *ret_signum = l;
        return true;
    }

    for (size_t i = 0; i < strlen(str); i++) {
        str[i] = toupper((unsigned char) str[i]);
    }

    return str2sig(str, ret_signum) >= 0;
}

static void usage(void) {
    fprintf(stderr, "usage: kill [-s SIGNAL] PID...\n"
                    "       kill -l\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    bool list = false;
    int signal = SIGTERM;

    int c;
    while ((c = getopt(argc, argv, "ls:")) != -1) {
        switch (c) {
            case 'l':
                list = true;
                break;
            case 's':
                if (!parse_signal(optarg, &signal)) {
                    warnx("invalid signal: %s", optarg);
                    usage();
                }
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (list) {
        for (int i = 1; i < NSIG; i++) {
            char buffer[SIG2STR_MAX];
            sig2str(i, buffer);
            printf("%s%c", buffer, (i == NSIG - 1) ? '\n' : ' ');
        }

        return EXIT_SUCCESS;
    }

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        pid_t pid;
        if (!parse_pid(argv[i], &pid)) {
            errx(EXIT_FAILURE, "invalid process identifier: %s", argv[i]);
        }

        if (kill(pid, signal) < 0) {
            warn("kill(%d, %d)", pid, signal);
            ret = EXIT_FAILURE;
        }
    }

    return ret;
}
